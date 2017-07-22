use std::io;
use std::rc::Rc;

use futures::{future, Future, Stream, Poll, Async};
use tokio_core::reactor;
use tokio_core::net::{TcpStream, TcpListener};

use super::v3;
use super::socks5;
use super::buffer::{BufRange, SharedBuf};
use super::transfer::{enc_transfer, dec_transfer};
use super::crypto::{KeyPair, Crypto};
use super::config::ClientConfig;
use super::util::RandomBytes;


pub struct Client {
    config: Rc<ClientConfig>,
    socks5_reply: Rc<socks5::Reply>,
}

impl Client {
    pub fn new(config: ClientConfig) -> Client {
        let reply = socks5::Reply::new(config.listen);
        Client {
            config: Rc::new(config),
            socks5_reply: Rc::new(reply),
        }
    }

    pub fn serve(&self) -> io::Result<()> {
        let mut core = reactor::Core::new()?;
        let handle = core.handle();

        let config = self.config.clone();
        let socks5_reply = self.socks5_reply.clone();

        let listener = TcpListener::bind(&config.listen, &handle).map_err(|e| {
            other(&format!("bind on {}, {}", config.listen, e))
        })?;

        info!("Listening for socks5 proxy on local {}", config.listen);

        let clients = listener.incoming().for_each(|(conn, addr)| {
            let client = Rc::new(conn);

            let socks5 = socks5::Handshake::new(
                handle.clone(),
                socks5_reply.clone(),
                client.clone(),
                config.server,
            );

            let handshake_config = config.clone();
            let handshake = socks5.and_then(move |(conn, addr)| {
                let config = handshake_config.clone();
                conn.set_nodelay(true).unwrap();
                let server = Rc::new(conn);
                match Handshake::new(config, server.clone(), addr) {
                    Ok(hand) => future::ok(hand.map(|keypair| (keypair, server))),
                    Err(e) => future::err(e),
                }.flatten()
            });

            let transfer_config = config.clone();
            let transfer = handshake.and_then(move |(keypair, server): (KeyPair, Rc<TcpStream>)| {
                let (ckey, skey) = keypair.split();
                let enc =
                    enc_transfer(client.clone(), server.clone(), transfer_config.cipher, ckey);
                let dec =
                    dec_transfer(server.clone(), client.clone(), transfer_config.cipher, skey);

                enc.join(dec)
            });

            handle.spawn(transfer.then(move |res| {
                match res {
                    Ok(x) => println!("proxied for {} {:?}", addr, x),
                    Err(e) => println!("error for {}: {}", addr, e),
                }
                future::ok(())
            }));
            Ok(())
        });

        core.run(clients).unwrap();
        Ok(())
    }
}


#[derive(Copy, Clone, Debug)]
enum HandshakeState {
    Init,
    ClientReq(BufRange),
    ServerRespHeader(BufRange),
    ServerRespData(BufRange),
    Done,
}

struct Handshake {
    config: Rc<ClientConfig>,
    server: Rc<TcpStream>,
    addr: socks5::ReqAddr,

    crypto: Crypto,
    buf: SharedBuf,
    state: HandshakeState,
}

impl Handshake {
    fn new(
        config: Rc<ClientConfig>,
        server: Rc<TcpStream>,
        addr: socks5::ReqAddr,
    ) -> io::Result<Handshake> {
        let crypto = Crypto::new(
            v3::HANDSHAKE_CIPHER,
            config.password.as_ref(),
            config.password.as_ref(),
        )?;
        Ok(Handshake {
            config: config,
            server: server,
            addr: addr,
            crypto: crypto,
            buf: SharedBuf::new(v3::MAX_BUFFER_SIZE),
            state: HandshakeState::Init,
        })
    }

    // +---------------+------+--------------------------------------------------+
    // |     Header    | Plain|                      Data                        |
    // +-----+---------+------+---------+-----+------+-----+----------+----------+
    // | LEN | LEN TAG | USER | PADDING | VER | CTYP |ATYP | DST.ADDR | DST.PORT |
    // +-----+---------+------+---------+-----+------+----------------+----------+
    // |  2  |   Var.  |  32  |   Var.  |  1  |   1  |  1  |   Var.   |    2     |
    // +-----+---------+------+---------+-----+------+-----+----------+----------+
    fn request(&mut self) -> io::Result<BufRange> {
        let tag_len = self.crypto.tag_len();
        let header_len = v3::DATA_LEN_LEN + tag_len;

        // USER
        let range = BufRange {
            start: header_len,
            end: header_len + v3::DEFAULT_DIGEST_LEN,
        };
        self.buf.copy_from_slice(
            range,
            self.config.username.as_ref(),
        );

        // PADDING
        let random_bytes = RandomBytes::new()?;
        let bytes = random_bytes.get();
        let range = BufRange {
            start: range.end,
            end: range.end + bytes.len(),
        };
        self.buf.copy_from_slice(range, bytes);

        // VER and CTYP
        let range = BufRange {
            start: range.end,
            end: range.end + 2,
        };
        {
            let b = self.buf.get_mut_range(range);
            b[0] = v3::VERSION;
            b[1] = self.config.cipher.to_no();
        }

        // ADDR
        let bytes = self.addr.get_bytes();
        let range = BufRange {
            start: range.end,
            end: range.end + bytes.len(),
        };
        self.buf.copy_from_slice(range, bytes);

        // Encrypt it
        let data_len = range.end - header_len;
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
            self.crypto.encrypt(head, 2)?;
        }
        {
            let len = data_len - v3::DEFAULT_DIGEST_LEN;
            let range = BufRange {
                start: header_len + v3::DEFAULT_DIGEST_LEN,
                end: range.end + tag_len,
            };
            let data = self.buf.get_mut_range(range);
            self.crypto.encrypt(data, len)?;
        }

        Ok(BufRange {
            start: 0,
            end: range.end + tag_len,
        })
    }
}

impl Future for Handshake {
    type Item = KeyPair;
    type Error = io::Error;

    fn poll(&mut self) -> Poll<KeyPair, io::Error> {
        loop {
            match self.state {
                HandshakeState::Init => {
                    let range = self.request()?;
                    self.state = HandshakeState::ClientReq(range);
                }
                HandshakeState::ClientReq(range) => {
                    try_ready!(self.buf.write_exact(&mut (&*self.server), range));

                    let resp_header_len = v3::DATA_LEN_LEN + self.crypto.tag_len();
                    self.state = HandshakeState::ServerRespHeader(BufRange {
                        start: 0,
                        end: resp_header_len,
                    });
                }
                HandshakeState::ServerRespHeader(range) => {
                    let range = try_ready!(self.buf.read_exact(&mut (&*self.server), range));
                    let buf = self.buf.get_mut_range(range);
                    let len = self.crypto.decrypt(buf)?;

                    // +-----+---------+
                    // | LEN | LEN TAG |
                    // +-----+---------+
                    // |  2  |   Var.  |
                    // +-----+---------+
                    debug_assert!(len == v3::DATA_LEN_LEN);
                    let data_len = ((buf[0] as usize) << 8) + (buf[1] as usize);
                    // Padding len 1 + resp 1
                    if data_len < 2 + self.crypto.tag_len() {
                        return Err(other("response data length too small"));
                    }

                    self.state = HandshakeState::ServerRespData(BufRange {
                        start: 0,
                        end: data_len,
                    });
                }
                HandshakeState::ServerRespData(range) => {
                    let range = try_ready!(self.buf.read_exact(&mut (&*self.server), range));
                    let len = {
                        let buf = self.buf.get_mut_range(range);
                        self.crypto.decrypt(buf)?
                    };

                    // +---------+-------+-------+------+
                    // | PADDING |  RESP |  EKEY | DKEY |
                    // +---------+-------+-------+------+
                    // |   Var.  |    1  |  Var. | Var. |
                    // +---------+-------+-------+------+
                    let buf = self.buf.get_ref_range(BufRange {
                        start: range.start,
                        end: range.start + len,
                    });
                    let padding_len = (buf[0] as usize) + 1;
                    if len < padding_len + 1 {
                        return Err(other("response data length too small"));
                    }

                    match buf[padding_len] {
                        v3::SERVER_RESP_SUCCESSD => debug!("server resp success"),
                        v3::SERVER_RESP_CIPHER_ERROR => {
                            return Err(other("server not support cipher"))
                        }
                        v3::SERVER_RESP_ERROR => return Err(other("server internal error")),
                        v3::SERVER_RESP_REMOTE_FAILED => {
                            return Err(other("server connect request addr error"))
                        }
                        _ => return Err(other("server resp unkown")),
                    }
                    let key_len = len - padding_len - 1;
                    if key_len != 2 * self.config.cipher.key_len() {
                        return Err(other("server resp key length not match"));
                    }

                    let keypair = KeyPair::from(&buf[padding_len + 1..]);

                    self.state = HandshakeState::Done;

                    return Ok(Async::Ready(keypair));
                }
                HandshakeState::Done => panic!("poll a done future"),
            }
        }
    }
}

fn other(desc: &str) -> io::Error {
    io::Error::new(io::ErrorKind::Other, desc)
}
