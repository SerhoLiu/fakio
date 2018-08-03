use std::io;
use std::sync::Arc;
use std::time::{Duration, Instant};

use futures::{future, Async, Future, Poll, Stream};
use tokio;
use tokio::net::TcpListener;
use tokio::timer;

use super::buffer::{BufRange, SharedBuf};
use super::config::ClientConfig;
use super::crypto::{Crypto, KeyPair};
use super::net::ProxyStream;
use super::socks5;
use super::transfer;
use super::util::RandomBytes;
use super::v3;

const HANDSHAKE_TIMEOUT: u64 = 10;

pub struct Client {
    config: ClientConfig,
    socks5_reply: socks5::Reply,
}

impl Client {
    pub fn new(config: ClientConfig) -> Client {
        let reply = socks5::Reply::new(config.listen);
        Client {
            config,
            socks5_reply: reply,
        }
    }

    pub fn serve(self) -> io::Result<()> {
        let config = Arc::new(self.config);
        let socks5_reply = Arc::new(self.socks5_reply);

        let listener = TcpListener::bind(&config.listen)
            .map_err(|e| other(&format!("bind on {}, {}", config.listen, e)))?;

        info!("Listening for socks5 proxy on local {}", config.listen);

        let clients = listener
            .incoming()
            .map_err(|e| error!("error accepting socket; error = {:?}", e))
            .for_each(move |conn| {
                let peer_addr = conn.peer_addr().unwrap();
                let client = ProxyStream::new(conn);

                let socks5 = socks5::Handshake::new(
                    socks5_reply.clone(),
                    peer_addr,
                    client.clone(),
                    config.server,
                );

                let handshake_config = config.clone();
                let handshake = socks5.and_then(move |(conn, reqaddr)| {
                    conn.set_nodelay(true).unwrap();

                    let config = handshake_config.clone();
                    let req = reqaddr.get().unwrap();
                    let req = format!("{}:{}", req.0, req.1);
                    let server = ProxyStream::new(conn);

                    match Handshake::new(config, server.clone(), reqaddr) {
                        Ok(hand) => future::ok(hand.map(|keypair| (req, keypair, server))),
                        Err(e) => future::err(e),
                    }.flatten()
                });

                let timeout = Instant::now() + Duration::from_secs(HANDSHAKE_TIMEOUT);
                let handshake =
                    timer::Deadline::new(handshake, timeout).map_err(|e| other(&format!("{}", e)));

                let transfer_config = config.clone();

                let transfer = handshake.and_then(move |(reqaddr, keypair, server)| {
                    let (ckey, skey) = keypair.split();
                    let trans = transfer::encrypt(
                        client.clone(),
                        server.clone(),
                        transfer_config.cipher,
                        ckey,
                    ).join(transfer::decrypt(
                        server.clone(),
                        client.clone(),
                        transfer_config.cipher,
                        skey,
                    ));

                    trans.map(|(enc, dec)| (reqaddr, transfer::Stat::new(enc, dec)))
                });

                tokio::spawn(transfer.then(move |res| {
                    match res {
                        Ok((req, stat)) => {
                            info!("{} - request {} success, {}", peer_addr, req, stat)
                        }
                        Err(e) => error!("{} - failed by {}", peer_addr, e),
                    }
                    future::ok(())
                }))
            });

        tokio::run(clients);

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
    config: Arc<ClientConfig>,
    server: ProxyStream,
    addr: socks5::ReqAddr,

    crypto: Crypto,
    buf: SharedBuf,
    state: HandshakeState,
}

impl Handshake {
    fn new(
        config: Arc<ClientConfig>,
        server: ProxyStream,
        addr: socks5::ReqAddr,
    ) -> io::Result<Handshake> {
        let crypto = Crypto::new(
            v3::HANDSHAKE_CIPHER,
            config.password.as_ref(),
            config.password.as_ref(),
        )?;
        Ok(Handshake {
            config,
            server,
            addr,
            crypto,
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
        self.buf
            .copy_from_slice(range, self.config.username.as_ref());

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
                    try_ready!(self.buf.write_exact(&mut self.server, range));

                    let resp_header_len = v3::DATA_LEN_LEN + self.crypto.tag_len();
                    self.state = HandshakeState::ServerRespHeader(BufRange {
                        start: 0,
                        end: resp_header_len,
                    });
                }
                HandshakeState::ServerRespHeader(range) => {
                    let range = try_ready!(self.buf.read_exact(&mut self.server, range));
                    let buf = self.buf.get_mut_range(range);
                    let len = self.crypto.decrypt(buf)?;

                    // +-----+---------+
                    // | LEN | LEN TAG |
                    // +-----+---------+
                    // |  2  |   Var.  |
                    // +-----+---------+
                    debug_assert_eq!(len, v3::DATA_LEN_LEN);
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
                    let range = try_ready!(self.buf.read_exact(&mut self.server, range));
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

#[inline]
fn other(desc: &str) -> io::Error {
    io::Error::new(io::ErrorKind::Other, desc)
}
