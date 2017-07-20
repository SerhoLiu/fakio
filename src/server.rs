use std::io;
use std::mem;
use std::rc::Rc;
use std::net::{SocketAddr, ToSocketAddrs};
use std::collections::HashMap;

use futures::{future, Future, Stream, Poll, Async};
use futures_cpupool::CpuPool;
use tokio_core::reactor;
use tokio_core::net::{TcpStream, TcpListener};

use super::v3;
use super::socks5;
use super::net::TcpConnect;
use super::util::RandomBytes;
use super::buffer::{BufRange, SharedBuf};
use super::transfer::{EncTransfer, DecTransfer};
use super::crypto::{self, KeyPair, Cipher, Crypto};
use super::config::{Digest, User, ServerConfig};


pub struct Server {
    config: ServerConfig,
    users: Rc<HashMap<Digest, User>>,
    cpu_pool: CpuPool,
}

impl Server {
    pub fn new(config: ServerConfig) -> Server {
        Server {
            users: Rc::new(config.users.clone()),
            config: config,
            cpu_pool: CpuPool::new_num_cpus(),
        }
    }

    pub fn serve(&self) -> io::Result<()> {
        let mut core = reactor::Core::new()?;
        let handle = core.handle();

        let listener = TcpListener::bind(&self.config.listen, &handle).map_err(
            |e| {
                other(&format!("bind on {}, {}", self.config.listen, e))
            },
        )?;

        info!("Listening for {}", self.config.listen);

        let server = listener.incoming().for_each(|(client, addr)| {
            //let addr = addr.clone();
            let client = Rc::new(client);
            let hand = Handshake::new(
                self.users.clone(),
                client.clone(),
                handle.clone(),
                self.cpu_pool.clone(),
            ).and_then(move |(remote, cipher, keypair)| {
                let len = keypair.len() / 2;
                let dkey = &keypair[..len];
                let ekey = &keypair[len..];

                let remote = Rc::new(remote);
                let enc = EncTransfer::new(remote.clone(), client.clone(), cipher, ekey).unwrap();
                let dec = DecTransfer::new(client.clone(), remote.clone(), cipher, dkey).unwrap();

                enc.join(dec)
            });
            handle.spawn(hand.then(move |res| {
                match res {
                    Ok(x) => {
                        info!("proxied for {} {:?}", addr, x)
                    }
                    Err(e) => error!("error for {}: {}", addr, e),
                }
                future::ok(())
            }));
            Ok(())
        });

        core.run(server).unwrap();
        Ok(())
    }
}


#[derive(Copy, Clone, Debug)]
enum HandshakeState {
    ReqHeader(BufRange),
    ReqData(BufRange),
    ConnectRemote,
    RespToClient(BufRange),
    Done,
}

struct Handshake {
    users: Rc<HashMap<Digest, User>>,
    client: Rc<TcpStream>,
    handle: reactor::Handle,
    cpu_pool: CpuPool,

    buf: SharedBuf,
    state: HandshakeState,

    req_addr: String,
    req_connect: Option<TcpConnect>,
    req_user: Option<User>,
    req_crypto: Option<Crypto>,

    req_remote: Option<TcpStream>,
    req_cipher: Option<Cipher>,
    req_keypair: Option<KeyPair>,
}

impl Handshake {
    fn new(
        users: Rc<HashMap<Digest, User>>,
        client: Rc<TcpStream>,
        handle: reactor::Handle,
        cpu_pool: CpuPool,
    ) -> Handshake {
        Handshake {
            users: users,
            client: client,
            handle: handle,
            cpu_pool: cpu_pool,
            buf: SharedBuf::new(v3::MAX_BUFFER_SIZE),
            state: HandshakeState::ReqHeader(BufRange {
                start: 0,
                end: v3::DATA_LEN_LEN + v3::HANDSHAKE_CIPHER.tag_len() + v3::DEFAULT_DIGEST_LEN,
            }),

            req_addr: String::new(),
            req_connect: None,
            req_user: None,
            req_crypto: None,

            req_remote: None,
            req_keypair: None,
            req_cipher: None,
        }
    }

    // +---------+-----+------+-----+----------+----------+
    // | PADDING | VER | CTYP |ATYP | DST.ADDR | DST.PORT |
    // +---------+-----+------+----------------+----------+
    // |   Var.  |  1  |   1  |  1  |   Var.   |    2     |
    // +---------+-----+------+-----+----------+----------+
    fn request(data: &[u8]) -> io::Result<(Cipher, socks5::ReqAddr)> {
        let data_len = data.len();
        let padding_len = (data[0] as usize) + 1;
        if data_len < padding_len + 1 + 1 + 1 + 1 {
            return Err(other("request data len not match"));
        }

        let buf = &data[padding_len..];
        if buf[0] != v3::VERSION {
            return Err(other(&format!(
                "request version not match, need {}, but {}",
                v3::VERSION,
                buf[0]
            )));
        }

        let cipher = Cipher::from_no(buf[1])?;

        let addr_start = padding_len + 1 + 1;
        let addr_len = match buf[2] {
            // 4 bytes ipv4 and 2 bytes port, and already read more 1 bytes
            socks5::ADDR_TYPE_IPV4 => 4 + 2 + 1,
            // 16 bytes ipv6 and 2 bytes port
            socks5::ADDR_TYPE_IPV6 => 16 + 2 + 1,
            socks5::ADDR_TYPE_DOMAIN_NAME => 1 + (buf[3] as usize) + 2 + 1,
            n => return Err(other(&format!("request atyp {} unknown", n))),
        };

        if data_len != addr_start + addr_len {
            return Err(other("request data len not match"));
        }

        let reqaddr = socks5::ReqAddr::new(&data[addr_start..data_len]);

        Ok((cipher, reqaddr))
    }

    // +---------------+--------------------------------+
    // |     Header    |              Data              |
    // +-----+---------+---------+-------+-------+------+
    // | LEN | LEN TAG | PADDING |  RESP |  EKEY | DKEY |
    // +-----+---------+---------+-------+-------+------+
    // |  2  |   Var.  |   Var.  |    1  |  Var. | Var. |
    // +-----+---------+---------+-------+-------+------+
    fn response(&mut self) -> io::Result<BufRange> {
        let user = self.req_user.as_ref().unwrap();
        let cipher = self.req_cipher.as_ref().unwrap();
        let crypto = self.req_crypto.as_mut().unwrap();

        let tag_len = crypto.tag_len();
        let header_len = v3::DATA_LEN_LEN + tag_len;

        // PADDING
        let random_bytes = RandomBytes::new()?;
        let bytes = random_bytes.get();
        let range = BufRange {
            start: header_len,
            end: header_len + bytes.len(),
        };
        self.buf.copy_from_slice(range, bytes);

        let key_len = cipher.key_len() * 2;
        let range = BufRange {
            start: range.end,
            end: range.end + 1 + key_len,
        };
        {
            let data = self.buf.get_mut_range(range);
            // RESP
            data[0] = match self.req_remote {
                Some(_) => v3::SERVER_RESP_SUCCESSD,
                None => v3::SERVER_RESP_REMOTE_FAILED,
            };
            // KEY
            let keypair = crypto::generate_key(user.password.as_ref(), key_len)?;
            data[1..].copy_from_slice(keypair.as_ref());
            self.req_keypair = Some(keypair);
        }

        // HEADER
        let data_range = BufRange {
            start: header_len,
            end: range.end + tag_len,
        };
        {
            let data_len = data_range.end - data_range.start;
            let range = BufRange {
                start: 0,
                end: header_len,
            };
            let head = self.buf.get_mut_range(range);

            // Big Endian
            head[0] = (data_len >> 8) as u8;
            head[1] = data_len as u8;
            crypto.encrypt(head, 2)?;
        }

        {
            let data = self.buf.get_mut_range(data_range);
            crypto.encrypt(data, data_range.end - header_len - tag_len)?;
        }

        Ok(BufRange {
            start: 0,
            end: data_range.end,
        })
    }
}

impl Future for Handshake {
    type Item = (TcpStream, Cipher, KeyPair);
    type Error = io::Error;

    fn poll(&mut self) -> Poll<(TcpStream, Cipher, KeyPair), io::Error> {
        loop {
            match self.state {

                // +-----+---------+------+
                // | LEN | LEN TAG | USER |
                // +-----+---------+------+
                // |  2  |   Var.  |  32  |
                // +-----+---------+------+
                HandshakeState::ReqHeader(range) => {
                    let range = try_ready!(self.buf.read_exact(&mut (&*self.client), range));
                    let user = {
                        let user = self.buf.get_ref_range(BufRange {
                            start: range.end - v3::DEFAULT_DIGEST_LEN,
                            end: range.end,
                        });
                        self.users.get(user).ok_or(other(
                            &format!("user ({:?}) not exists", user),
                        ))?
                    };

                    let mut crypto = Crypto::new(
                        v3::HANDSHAKE_CIPHER,
                        user.password.as_ref(),
                        user.password.as_ref(),
                    )?;
                    let header = self.buf.get_mut_range(BufRange {
                        start: 0,
                        end: v3::DATA_LEN_LEN + crypto.tag_len(),
                    });

                    let size = crypto.decrypt(header)?;
                    assert!(size == v3::DATA_LEN_LEN);
                    let len = ((header[0] as usize) << 8) + (header[1] as usize);

                    self.req_user = Some(user.clone());
                    self.req_crypto = Some(crypto);
                    self.state = HandshakeState::ReqData(BufRange {
                        start: 0,
                        end: len - v3::DEFAULT_DIGEST_LEN,
                    });
                }
                HandshakeState::ReqData(range) => {
                    let range = try_ready!(self.buf.read_exact(&mut (&*self.client), range));

                    let crypto = self.req_crypto.as_mut().unwrap();
                    let user = self.req_user.as_ref().unwrap();
                    let len = {
                        let buf = self.buf.get_mut_range(range);
                        crypto.decrypt(buf)?
                    };
                    let buf = self.buf.get_ref_range(BufRange {
                        start: range.start,
                        end: range.start + len,
                    });
                    let (cihper, reqaddr) = Handshake::request(buf)?;
                    let addr = reqaddr.get()?;
                    self.req_addr = format!("{}:{}", addr.0, addr.1);
                    self.req_cipher = Some(cihper);

                    info!(
                        "request by {}, {}, cihper {}",
                        self.req_addr,
                        user.name,
                        cihper
                    );
                    self.req_connect = Some(TcpConnect::new(
                        addr,
                        self.cpu_pool.clone(),
                        self.handle.clone(),
                    ));
                    self.state = HandshakeState::ConnectRemote;
                }
                HandshakeState::ConnectRemote => {
                    match self.req_connect {
                        Some(ref mut s) => {
                            match s.poll() {
                                Ok(Async::Ready(conn)) => self.req_remote = Some(conn),
                                Ok(Async::NotReady) => return Ok(Async::NotReady),
                                Err(e) => warn!("connect {}, {}", self.req_addr, e),
                            }
                        }
                        None => panic!("connect server on illegal state"),
                    };
                    let range = self.response()?;
                    self.state = HandshakeState::RespToClient(range);
                }
                HandshakeState::RespToClient(range) => {
                    try_ready!(self.buf.write_exact(&mut (&*self.client), range));
                    self.state = HandshakeState::Done;
                    let remote = match mem::replace(&mut self.req_remote, None) {
                        Some(conn) => conn,
                        None => return Err(other("remote connect failed")),
                    };
                    let cipher = self.req_cipher.unwrap();
                    let keypair = self.req_keypair.as_ref().unwrap().clone();

                    return Ok(Async::Ready((remote, cipher, keypair)))

                }
                HandshakeState::Done => panic!("poll a done future"),
            }
        }
    }
}

fn other(desc: &str) -> io::Error {
    io::Error::new(io::ErrorKind::Other, desc)
}
