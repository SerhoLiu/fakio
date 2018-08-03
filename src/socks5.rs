//! Socks5 protocol definition ([RFC1928](https://tools.ietf.org/rfc/rfc1928.txt))

use std::io;
use std::mem;
use std::net::{self, SocketAddr};
use std::str;
use std::sync::Arc;

use futures::{Async, Future, Poll};
use tokio::net::{ConnectFuture, TcpStream};

use super::buffer::{BufRange, SharedBuf};
use super::net::ProxyStream;

const VERSION: u8 = 0x05;
const AUTH_METHOD_NONE: u8 = 0x00;
const CMD_TCP_CONNECT: u8 = 0x01;

pub const ADDR_TYPE_IPV4: u8 = 0x01;
pub const ADDR_TYPE_IPV6: u8 = 0x04;
pub const ADDR_TYPE_DOMAIN_NAME: u8 = 0x03;

const REPLY_SUCCEEDED: u8 = 0x00;
const REPLY_GENERAL_FAILURE: u8 = 0x01;
const REPLY_CONNECTION_REFUSED: u8 = 0x05;

// +----+-----+-------+------+----------+----------+
// |VER | REP |  RSV  | ATYP | BND.ADDR | BND.PORT |
// +----+-----+-------+------+----------+----------+
// | 1  |  1  | X'00' |  1   | Variable |    2     |
// +----+-----+-------+------+----------+----------+

const MAX_REQ_LEN: usize = 1 + 1 + 1 + 1 + 1 + 255 + 2;

pub struct ReqAddr {
    len: usize,
    bytes: [u8; MAX_REQ_LEN],
}

impl ReqAddr {
    pub fn new(bytes: &[u8]) -> ReqAddr {
        let len = bytes.len();
        assert!(len < MAX_REQ_LEN);

        let mut buf = [0u8; MAX_REQ_LEN];
        buf[..len].copy_from_slice(bytes);
        ReqAddr { len, bytes: buf }
    }

    #[inline]
    pub fn get_bytes(&self) -> &[u8] {
        &self.bytes[..self.len]
    }

    pub fn get(&self) -> io::Result<(String, u16)> {
        let atyp = self.bytes[0];
        let buf = &self.bytes[1..self.len];

        match atyp {
            ADDR_TYPE_IPV4 => {
                let addr = net::Ipv4Addr::new(buf[0], buf[1], buf[2], buf[3]);
                let port = (u16::from(buf[4]) << 8) | u16::from(buf[5]);
                Ok((format!("{}", addr), port))
            }
            ADDR_TYPE_IPV6 => {
                let a = (u16::from(buf[0]) << 8) | u16::from(buf[1]);
                let b = (u16::from(buf[2]) << 8) | u16::from(buf[3]);
                let c = (u16::from(buf[4]) << 8) | u16::from(buf[5]);
                let d = (u16::from(buf[6]) << 8) | u16::from(buf[7]);
                let e = (u16::from(buf[8]) << 8) | u16::from(buf[9]);
                let f = (u16::from(buf[10]) << 8) | u16::from(buf[11]);
                let g = (u16::from(buf[12]) << 8) | u16::from(buf[13]);
                let h = (u16::from(buf[14]) << 8) | u16::from(buf[15]);
                let addr = net::Ipv6Addr::new(a, b, c, d, e, f, g, h);
                let port = (u16::from(buf[16]) << 8) | u16::from(buf[17]);

                Ok((format!("{}", addr), port))
            }
            ADDR_TYPE_DOMAIN_NAME => {
                let len = buf[0] as usize;
                let domain = String::from_utf8(buf[1..len + 1].to_vec())
                    .map_err(|e| other(&format!("domain not valid utf-8, {}", e)))?;
                let pos = buf.len() - 2;
                let port = (u16::from(buf[pos]) << 8) | u16::from(buf[pos + 1]);
                Ok((domain, port))
            }
            _ => unreachable!(),
        }
    }
}

// BND.ADDR use IP V4 or V6, 16 is ipv6 bytes
pub const REPLY_LEN: usize = 1 + 1 + 1 + 1 + 16 + 2;

pub struct Reply {
    len: usize,
    buffer: [u8; REPLY_LEN],
}

impl Reply {
    pub fn new(addr: SocketAddr) -> Reply {
        let mut buf = [0u8; REPLY_LEN];

        // VER - protocol version
        buf[0] = VERSION;

        // RSV - reserved
        buf[2] = 0;

        let pos = match addr {
            SocketAddr::V4(ref a) => {
                buf[3] = ADDR_TYPE_IPV4;
                buf[4..8].copy_from_slice(&a.ip().octets()[..]);
                8
            }
            SocketAddr::V6(ref a) => {
                buf[3] = ADDR_TYPE_IPV6;
                let mut pos = 4;
                for &segment in &a.ip().segments() {
                    buf[pos] = (segment >> 8) as u8;
                    buf[pos + 1] = segment as u8;
                    pos += 2;
                }
                pos
            }
        };
        buf[pos] = (addr.port() >> 8) as u8;
        buf[pos + 1] = addr.port() as u8;

        Reply {
            len: pos + 2,
            buffer: buf,
        }
    }

    fn get(&self, rtype: u8, out: &mut [u8]) -> usize {
        assert!(out.len() >= self.len);
        out[..self.len].copy_from_slice(&self.buffer[..self.len]);
        // REP
        out[1] = rtype;

        self.len
    }
}

pub struct Handshake {
    peer_addr: SocketAddr,
    client: ProxyStream,
    reply: Arc<Reply>,
    remote_addr: SocketAddr,

    buf: SharedBuf,
    state: HandshakeState,

    connect: Option<ConnectFuture>,
    remote: io::Result<TcpStream>,
    reqaddr: Option<ReqAddr>,
}

impl Handshake {
    pub fn new(
        reply: Arc<Reply>,
        peer: SocketAddr,
        client: ProxyStream,
        remote: SocketAddr,
    ) -> Handshake {
        Handshake {
            peer_addr: peer,
            client,
            reply,
            remote_addr: remote,

            buf: SharedBuf::new(MAX_REQ_LEN),
            state: HandshakeState::AuthVersion(BufRange { start: 0, end: 1 }),

            connect: None,
            remote: Err(not_connected()),
            reqaddr: None,
        }
    }
}

#[derive(Copy, Clone, Debug)]
enum HandshakeState {
    AuthVersion(BufRange),
    AuthNmethod(BufRange),
    AuthMethods(BufRange),
    AuthEnd(BufRange),

    ReqVersion(BufRange),
    ReqHeader(BufRange),
    ReqData(BufRange),

    ConnectServer,

    ReqEnd(BufRange),

    Done,
}

impl Future for Handshake {
    type Item = (TcpStream, ReqAddr);
    type Error = io::Error;

    fn poll(&mut self) -> Poll<(TcpStream, ReqAddr), io::Error> {
        loop {
            match self.state {
                HandshakeState::AuthVersion(range) => {
                    let range = try_ready!(self.buf.read_exact(&mut self.client, range));
                    let buf = self.buf.get_ref_range(range);
                    if buf[0] != VERSION {
                        return Err(other("only support socks version 5"));
                    }
                    self.state = HandshakeState::AuthNmethod(BufRange {
                        start: range.end,
                        end: range.end + 1,
                    });
                }
                HandshakeState::AuthNmethod(range) => {
                    let range = try_ready!(self.buf.read_exact(&mut self.client, range));
                    let buf = self.buf.get_ref_range(range);
                    self.state = HandshakeState::AuthMethods(BufRange {
                        start: range.end,
                        end: range.end + buf[0] as usize,
                    });
                }
                HandshakeState::AuthMethods(range) => {
                    let range = try_ready!(self.buf.read_exact(&mut self.client, range));
                    {
                        let buf = self.buf.get_ref_range(range);
                        if !buf.contains(&AUTH_METHOD_NONE) {
                            return Err(other("no supported auth method given"));
                        }
                    }

                    let range = BufRange { start: 0, end: 2 };
                    let buf = self.buf.get_mut_range(range);
                    buf[0] = VERSION;
                    buf[1] = AUTH_METHOD_NONE;
                    self.state = HandshakeState::AuthEnd(range)
                }
                HandshakeState::AuthEnd(range) => {
                    try_ready!(self.buf.write_exact(&mut self.client, range));
                    self.state = HandshakeState::ReqVersion(BufRange { start: 0, end: 1 })
                }
                HandshakeState::ReqVersion(range) => {
                    let range = try_ready!(self.buf.read_exact(&mut self.client, range));
                    let buf = self.buf.get_ref_range(range);
                    if buf[0] != VERSION {
                        return Err(other("only support socks version 5"));
                    }
                    self.state = HandshakeState::ReqHeader(BufRange {
                        start: range.end,
                        // CMD +  RSV  + ATYP + 1
                        end: range.end + 4,
                    });
                }
                HandshakeState::ReqHeader(range) => {
                    let range = try_ready!(self.buf.read_exact(&mut self.client, range));
                    let buf = self.buf.get_ref_range(range);
                    if buf[0] != CMD_TCP_CONNECT {
                        return Err(other("only support tcp connect command"));
                    }
                    let need = match buf[2] {
                        // 4 bytes ipv4 and 2 bytes port, and already read more 1 bytes
                        ADDR_TYPE_IPV4 => 4 + 2 - 1,
                        // 16 bytes ipv6 and 2 bytes port
                        ADDR_TYPE_IPV6 => 16 + 2 - 1,
                        ADDR_TYPE_DOMAIN_NAME => (buf[3] as usize) + 2,
                        n => {
                            return Err(other(&format!("unknown ATYP received: {}", n)));
                        }
                    };
                    self.state = HandshakeState::ReqData(BufRange {
                        start: range.end,
                        end: range.end + need,
                    });
                }
                HandshakeState::ReqData(range) => {
                    let range = try_ready!(self.buf.read_exact(&mut self.client, range));
                    // get addr info
                    let range = BufRange {
                        // VER - CMD -  RSV
                        start: 1 + 1 + 1,
                        end: range.end,
                    };

                    let reqaddr = ReqAddr::new(self.buf.get_ref_range(range));
                    let addr = reqaddr.get()?;
                    info!("{} - request {}:{}", self.peer_addr, addr.0, addr.1);

                    self.reqaddr = Some(reqaddr);
                    self.connect = Some(TcpStream::connect(&self.remote_addr));
                    self.state = HandshakeState::ConnectServer;
                }
                HandshakeState::ConnectServer => {
                    // now, connect remote server
                    let rtype = match self.connect {
                        Some(ref mut s) => match s.poll() {
                            Ok(Async::Ready(conn)) => {
                                self.remote = Ok(conn);
                                REPLY_SUCCEEDED
                            }
                            Ok(Async::NotReady) => return Ok(Async::NotReady),
                            Err(e) => {
                                let kind = e.kind();
                                self.remote = Err(e);
                                if kind == io::ErrorKind::ConnectionRefused {
                                    REPLY_CONNECTION_REFUSED
                                } else {
                                    REPLY_GENERAL_FAILURE
                                }
                            }
                        },
                        None => panic!("connect server on illegal state"),
                    };

                    let reply_len = self.reply.get(rtype, self.buf.get_mut());
                    self.state = HandshakeState::ReqEnd(BufRange {
                        start: 0,
                        end: reply_len,
                    })
                }
                HandshakeState::ReqEnd(range) => {
                    try_ready!(self.buf.write_exact(&mut self.client, range));
                    self.state = HandshakeState::Done;
                    let remote = mem::replace(&mut self.remote, Err(not_connected()));
                    let reqaddr = mem::replace(&mut self.reqaddr, None);
                    match remote {
                        Ok(conn) => return Ok(Async::Ready((conn, reqaddr.unwrap()))),
                        Err(e) => {
                            return Err(other(&format!(
                                "connect remote {}, {}",
                                self.remote_addr, e
                            )))
                        }
                    }
                }
                HandshakeState::Done => panic!("poll a done future"),
            };
        }
    }
}

#[inline]
fn other(desc: &str) -> io::Error {
    io::Error::new(io::ErrorKind::Other, desc)
}

#[inline]
fn not_connected() -> io::Error {
    io::Error::new(io::ErrorKind::NotConnected, "not connected")
}
