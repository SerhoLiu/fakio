use std::collections::HashMap;
use std::io;
use std::mem;
use std::net::SocketAddr;
use std::sync::Arc;
use std::time::{Duration, Instant};

use futures::{future, Async, Future, Poll, Stream};
use rand;
use tokio;
use tokio::net::{TcpListener, TcpStream};
use tokio::timer;

use super::buffer::{BufRange, SharedBuf};
use super::config::{Digest, ServerConfig, User};
use super::crypto::{Cipher, Crypto, KeyPair};
use super::net::{ProxyStream, TcpConnect};
use super::socks5;
use super::transfer;
use super::util::{self, RandomBytes};
use super::v3;

pub struct Server {
    config: ServerConfig,
}

impl Server {
    pub fn new(config: ServerConfig) -> Server {
        Server { config }
    }

    pub fn serve(self) -> io::Result<()> {
        let users = Arc::new(self.config.users);

        let listen = self.config.listen;
        let listener =
            TcpListener::bind(&listen).map_err(|e| other(&format!("bind on {}, {}", listen, e)))?;

        info!("Listening on {}", listen);

        let server = listener
            .incoming()
            .map_err(|e| error!("accepting socket, {}", e))
            .for_each(move |client| {
                let peer_addr = client.peer_addr().unwrap();
                let client = ProxyStream::new(client);

                let handshake = Handshake::new(peer_addr, users.clone(), client.clone());

                // timeout random [10, 40)
                let secs = u64::from(rand::random::<u8>() % 30 + 10);
                let timeout = Instant::now() + Duration::from_secs(secs);

                let handshake = timer::Deadline::new(handshake, timeout)
                    .map_err(|e| other(&format!("client handshake timeout, {}", e)));
                let transfer = handshake.and_then(move |context| {
                    let (ckey, skey) = context.keypair.split();
                    let trans = transfer::encrypt(
                        context.remote.clone(),
                        context.client.clone(),
                        context.cipher,
                        skey,
                    )
                    .join(transfer::decrypt(
                        context.client.clone(),
                        context.remote.clone(),
                        context.cipher,
                        ckey,
                    ));

                    let user = context.user;
                    let reqaddr = context.addr;
                    trans.map(move |(enc, dec)| (user, reqaddr, transfer::Stat::new(enc, dec)))
                });

                tokio::spawn(transfer.then(move |res| {
                    match res {
                        Ok((user, req, stat)) => info!(
                            "{} - ({}) request {} success, {}",
                            peer_addr, user.name, req, stat,
                        ),
                        Err(e) => error!("{} - failed by {}", peer_addr, e),
                    }
                    future::ok(())
                }))
            });

        tokio::run(server);

        Ok(())
    }
}

struct Context {
    user: User,
    addr: String,
    cipher: Cipher,
    keypair: KeyPair,
    client: ProxyStream,
    remote: ProxyStream,
}

#[derive(Copy, Clone, Debug)]
enum HandshakeState {
    ReqHeader(BufRange),
    ReqData(BufRange),
    ConnectRemote,
    GenResponse,
    RespToClient(BufRange),
    Done,
}

struct Request {
    success: bool,
    addr: String,
    connect: Option<TcpConnect>,
    user: Option<User>,
    crypto: Option<Crypto>,

    remote: Option<TcpStream>,
    cipher: Option<Cipher>,
    keypair: Option<KeyPair>,
}

impl Request {
    pub fn none() -> Request {
        Request {
            success: false,
            addr: String::new(),
            connect: None,
            user: None,
            crypto: None,
            remote: None,
            cipher: None,
            keypair: None,
        }
    }
}

struct Handshake {
    peer_addr: SocketAddr,
    users: Arc<HashMap<Digest, User>>,
    client: ProxyStream,

    buf: SharedBuf,
    state: HandshakeState,

    req: Request,
}

impl Handshake {
    fn new(peer: SocketAddr, users: Arc<HashMap<Digest, User>>, client: ProxyStream) -> Handshake {
        Handshake {
            peer_addr: peer,
            users,
            client,
            buf: SharedBuf::new(v3::MAX_BUFFER_SIZE),
            state: HandshakeState::ReqHeader(BufRange {
                start: 0,
                end: v3::DATA_LEN_LEN + v3::HANDSHAKE_CIPHER.tag_len() + v3::DEFAULT_DIGEST_LEN,
            }),
            req: Request::none(),
        }
    }

    // +-----+---------+------+
    // | LEN | LEN TAG | USER |
    // +-----+---------+------+
    // |  2  |   Var.  |  32  |
    // +-----+---------+------+
    fn parse_header(&mut self, range: BufRange) -> io::Result<usize> {
        let user = {
            let user = self.buf.get_ref_range(BufRange {
                start: range.end - v3::DEFAULT_DIGEST_LEN,
                end: range.end,
            });
            self.users
                .get(user)
                .ok_or_else(|| other(&format!("user ({}) not exists", util::to_hex(user))))?
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
        debug_assert_eq!(size, v3::DATA_LEN_LEN);

        let len = ((header[0] as usize) << 8) + (header[1] as usize);
        if len <= v3::DEFAULT_DIGEST_LEN + crypto.tag_len() {
            return Err(other(&format!(
                "({}) request data length too small",
                user.name
            )));
        }

        self.req.user = Some(user.clone());
        self.req.crypto = Some(crypto);

        Ok(len - v3::DEFAULT_DIGEST_LEN)
    }

    // +---------+-----+------+-----+----------+----------+
    // | PADDING | VER | CTYP |ATYP | DST.ADDR | DST.PORT |
    // +---------+-----+------+----------------+----------+
    // |   Var.  |  1  |   1  |  1  |   Var.   |    2     |
    // +---------+-----+------+-----+----------+----------+
    fn parse_data(&mut self, range: BufRange) -> io::Result<Option<TcpConnect>> {
        let crypto = self.req.crypto.as_mut().unwrap();
        let user = self.req.user.as_ref().unwrap();

        let data_len = {
            let buf = self.buf.get_mut_range(range);
            crypto.decrypt(buf)?
        };
        let data = self.buf.get_ref_range(BufRange {
            start: range.start,
            end: range.start + data_len,
        });

        let padding_len = (data[0] as usize) + 1;
        if data_len < padding_len + 1 + 1 + 1 + 1 {
            return Err(other(&format!(
                "({}) request data length not match",
                user.name
            )));
        }

        let buf = &data[padding_len..];
        if buf[0] != v3::VERSION {
            return Err(other(&format!(
                "({}) request version not match, need {}, but {}",
                user.name,
                v3::VERSION,
                buf[0]
            )));
        }

        let cipher_no = buf[1];

        let addr_start = padding_len + 1 + 1;
        let addr_len = match buf[2] {
            // 4 bytes ipv4 and 2 bytes port, and already read more 1 bytes
            socks5::ADDR_TYPE_IPV4 => 4 + 2 + 1,
            // 16 bytes ipv6 and 2 bytes port
            socks5::ADDR_TYPE_IPV6 => 16 + 2 + 1,
            socks5::ADDR_TYPE_DOMAIN_NAME => 1 + (buf[3] as usize) + 2 + 1,
            n => {
                return Err(other(&format!(
                    "({}) request atyp {} unknown",
                    user.name, n
                )))
            }
        };

        if data_len != addr_start + addr_len {
            return Err(other(&format!(
                "({}) request data length not match",
                user.name
            )));
        }

        let addr = socks5::ReqAddr::new(&data[addr_start..data_len])
            .get()
            .map_err(|err| other(&format!("({}) request req addr error, {}", user.name, err)))?;

        self.req.addr = format!("{}:{}", addr.0, addr.1);

        match Cipher::from_no(cipher_no) {
            Ok(cipher) => {
                self.req.cipher = Some(cipher);
                info!(
                    "{} - ({}) request {}, cihper {}",
                    self.peer_addr, user.name, self.req.addr, cipher,
                );
                Ok(Some(TcpConnect::new(addr)))
            }
            Err(_) => {
                info!(
                    "{} - ({}) request {}, cipher '{}' not support",
                    self.peer_addr, user.name, self.req.addr, cipher_no,
                );
                Ok(None)
            }
        }
    }

    // +---------------+--------------------------------+
    // |     Header    |              Data              |
    // +-----+---------+---------+-------+-------+------+
    // | LEN | LEN TAG | PADDING |  RESP |  EKEY | DKEY |
    // +-----+---------+---------+-------+-------+------+
    // |  2  |   Var.  |   Var.  |    1  |  Var. | Var. |
    // +-----+---------+---------+-------+-------+------+
    fn response(&mut self) -> io::Result<BufRange> {
        let user = self.req.user.as_ref().unwrap();
        let crypto = self.req.crypto.as_mut().unwrap();

        let tag_len = crypto.tag_len();
        let header_len = v3::DATA_LEN_LEN + tag_len;
        let mut data_len = 0;

        // PADDING
        let random_bytes = RandomBytes::new()?;
        let bytes = random_bytes.get();
        let range = BufRange {
            start: header_len,
            end: header_len + bytes.len(),
        };
        self.buf.copy_from_slice(range, bytes);
        data_len += bytes.len();

        let mut resp = v3::SERVER_RESP_SUCCESSD;

        self.req.keypair = match self.req.cipher {
            Some(cipher) => match KeyPair::generate(user.password.as_ref(), cipher) {
                Ok(key) => Some(key),
                Err(e) => {
                    error!(
                        "{} - ({}) request generate key, {}",
                        self.peer_addr, user.name, e,
                    );
                    resp = v3::SERVER_RESP_ERROR;
                    None
                }
            },
            None => {
                resp = v3::SERVER_RESP_CIPHER_ERROR;
                None
            }
        };
        if self.req.keypair.is_some() && self.req.remote.is_none() {
            resp = v3::SERVER_RESP_REMOTE_FAILED;
        }

        // RESP
        let range = BufRange {
            start: range.end,
            end: range.end + 1,
        };
        self.buf.copy_from_slice(range, &[resp]);
        data_len += 1;

        // KEY
        if resp == v3::SERVER_RESP_SUCCESSD {
            let key = self.req.keypair.as_ref().unwrap();
            let range = BufRange {
                start: range.end,
                end: range.end + key.len(),
            };
            self.buf.copy_from_slice(range, key.as_ref());
            data_len += key.len();
        }

        // Encrypted Data
        let data_range = BufRange {
            start: header_len,
            end: header_len + data_len + tag_len,
        };
        {
            let range = BufRange {
                start: 0,
                end: header_len,
            };
            let head = self.buf.get_mut_range(range);

            // Big Endian
            let data_len = data_len + tag_len;
            head[0] = (data_len >> 8) as u8;
            head[1] = data_len as u8;
            crypto.encrypt(head, 2)?;
        }

        let data = self.buf.get_mut_range(data_range);
        crypto.encrypt(data, data_len)?;

        if resp == v3::SERVER_RESP_SUCCESSD {
            self.req.success = true;
        }

        Ok(BufRange {
            start: 0,
            end: data_range.end,
        })
    }
}

impl Future for Handshake {
    type Item = Context;
    type Error = io::Error;

    fn poll(&mut self) -> Poll<Context, io::Error> {
        loop {
            match self.state {
                // +-----+---------+------+
                // | LEN | LEN TAG | USER |
                // +-----+---------+------+
                // |  2  |   Var.  |  32  |
                // +-----+---------+------+
                HandshakeState::ReqHeader(range) => {
                    let range = try_ready!(self.buf.read_exact(&mut self.client, range));
                    let need = self.parse_header(range)?;
                    self.state = HandshakeState::ReqData(BufRange {
                        start: 0,
                        end: need,
                    });
                }
                HandshakeState::ReqData(range) => {
                    let range = try_ready!(self.buf.read_exact(&mut self.client, range));
                    self.state = match self.parse_data(range)? {
                        Some(connect) => {
                            self.req.connect = Some(connect);
                            HandshakeState::ConnectRemote
                        }
                        None => HandshakeState::GenResponse,
                    }
                }
                HandshakeState::ConnectRemote => {
                    match self.req.connect {
                        Some(ref mut s) => match s.poll() {
                            Ok(Async::Ready(conn)) => self.req.remote = Some(conn),
                            Ok(Async::NotReady) => return Ok(Async::NotReady),
                            Err(e) => {
                                let user = self.req.user.as_ref().unwrap();
                                error!(
                                    "{} - ({}) connect remote {}, {}",
                                    self.peer_addr, user.name, self.req.addr, e,
                                )
                            }
                        },
                        None => panic!("connect server on illegal state"),
                    };
                    self.state = HandshakeState::GenResponse;
                }
                HandshakeState::GenResponse => {
                    let range = self.response()?;
                    self.state = HandshakeState::RespToClient(range);
                }
                HandshakeState::RespToClient(range) => {
                    try_ready!(self.buf.write_exact(&mut self.client, range));
                    self.state = HandshakeState::Done;

                    let req = mem::replace(&mut self.req, Request::none());
                    let user = req.user.unwrap();
                    let addr = req.addr;

                    let remote = match (req.remote, req.success) {
                        (Some(conn), true) => conn,
                        _ => return Err(other(&format!("({}) request {} failed", user.name, addr))),
                    };

                    return Ok(Async::Ready(Context {
                        user,
                        addr,
                        cipher: req.cipher.unwrap(),
                        keypair: req.keypair.unwrap(),
                        client: self.client.clone(),
                        remote: ProxyStream::new(remote),
                    }));
                }
                HandshakeState::Done => panic!("poll a done future"),
            }
        }
    }
}

fn other(desc: &str) -> io::Error {
    io::Error::new(io::ErrorKind::Other, desc)
}
