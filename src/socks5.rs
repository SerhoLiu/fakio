//! Socks5 protocol definition ([RFC1928](https://tools.ietf.org/rfc/rfc1928.txt))

use std::io;
use std::net::{self, SocketAddr};
use std::str;

use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;

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

pub async fn handshake(
    client: &mut TcpStream,
    client_addr: SocketAddr,
    remote_addr: SocketAddr,
    reply: &Reply,
) -> io::Result<(TcpStream, ReqAddr)> {
    let mut buf = [0u8; MAX_REQ_LEN];

    // auth version
    client.read_exact(&mut buf[..1]).await?;
    if buf[0] != VERSION {
        return Err(other("only support socks version 5"));
    }

    // auth n method
    client.read_exact(&mut buf[..1]).await?;
    let n_method = buf[0] as usize;

    // methods
    client.read_exact(&mut buf[..n_method]).await?;
    if !(&buf[..n_method]).contains(&AUTH_METHOD_NONE) {
        return Err(other("no supported auth method given"));
    }

    // resp auth
    buf[0] = VERSION;
    buf[1] = AUTH_METHOD_NONE;
    client.write_all(&buf[..2]).await?;

    // req version
    client.read_exact(&mut buf[..1]).await?;
    if buf[0] != VERSION {
        return Err(other("only support socks version 5"));
    }

    // CMD +  RSV  + ATYP + 1
    let n = 4usize;
    client.read_exact(&mut buf[..n]).await?;
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

    client.read_exact(&mut buf[n..n + need]).await?;
    let req_addr = ReqAddr::new(&buf[2..n + need]);
    let addr = req_addr.get()?;
    info!("{} - request {}:{}", client_addr, addr.0, addr.1);

    let remote = TcpStream::connect(&remote_addr).await;

    let rtype = if let Err(ref e) = remote {
        if e.kind() == io::ErrorKind::ConnectionRefused {
            REPLY_CONNECTION_REFUSED
        } else {
            REPLY_GENERAL_FAILURE
        }
    } else {
        REPLY_SUCCEEDED
    };

    let reply_len = reply.get(rtype, &mut buf[..]);
    client.write_all(&mut buf[..reply_len]).await?;

    match remote {
        Ok(conn) => Ok((conn, req_addr)),
        Err(e) => Err(other(&format!("connect remote {}, {}", remote_addr, e))),
    }
}

#[inline]
fn other(desc: &str) -> io::Error {
    io::Error::new(io::ErrorKind::Other, desc)
}
