use std::io;
use std::net::SocketAddr;
use std::sync::Arc;

use futures::future::try_join;
use tokio;
use tokio::net::TcpListener;
use tokio::net::TcpStream;
use tokio::prelude::*;
use tokio::time::{timeout, Duration};

use super::config::ClientConfig;
use super::crypto::{Crypto, KeyPair};
use super::socks5;
use super::transfer::{self, Stat};
use super::util::RandomBytes;
use super::v3;

const HANDSHAKE_TIMEOUT: u64 = 10;

pub async fn serve(config: ClientConfig) -> io::Result<()> {
    let config = Arc::new(config);
    let socks5_reply = Arc::new(socks5::Reply::new(config.listen));

    let mut listener = TcpListener::bind(&config.listen)
        .await
        .map_err(|e| other(&format!("listen on {}, {}", config.listen, e)))?;

    info!("Listening for socks5 proxy on local {}", config.listen);

    loop {
        match listener.accept().await {
            Ok((client, peer_addr)) => {
                let config = config.clone();
                let socks5_reply = socks5_reply.clone();

                tokio::spawn(async move {
                    match process(config, socks5_reply, client, peer_addr).await {
                        Ok((req, stat)) => {
                            info!("{} - request {} success, {}", peer_addr, req, stat)
                        }
                        Err(e) => error!("{} - failed by {}", peer_addr, e),
                    }
                });
            }
            Err(e) => error!("accepting socket, {}", e),
        }
    }
}

async fn process(
    config: Arc<ClientConfig>,
    socks5_reply: Arc<socks5::Reply>,
    mut client: TcpStream,
    client_addr: SocketAddr,
) -> io::Result<(String, Stat)> {
    let handshake = async {
        let (mut server, req_addr) =
            socks5::handshake(&mut client, client_addr, config.server, &socks5_reply).await?;
        let req = req_addr.get()?;
        let req = format!("{}:{}", req.0, req.1);
        let mut handshake = Handshake::new(&config, &mut server, req_addr)?;
        let key_pair = handshake.hand().await?;
        Ok::<_, io::Error>((server, req, key_pair))
    };

    let (mut server, req_addr, key_pair) =
        timeout(Duration::from_secs(HANDSHAKE_TIMEOUT), handshake).await??;
    let (client_key, server_key) = key_pair.split();

    let (cr, cw) = client.split();
    let (sr, sw) = server.split();

    let en = transfer::encrypt(cr, sw, config.cipher, client_key);
    let de = transfer::decrypt(sr, cw, config.cipher, server_key);

    let stat = try_join(en, de).await?;
    Ok((req_addr, Stat::new(stat.0, stat.1)))
}

struct Handshake<'a> {
    config: &'a ClientConfig,
    server: &'a mut TcpStream,
    req_addr: socks5::ReqAddr,

    crypto: Crypto,
}

impl<'a> Handshake<'a> {
    fn new(
        config: &'a ClientConfig,
        server: &'a mut TcpStream,
        req_addr: socks5::ReqAddr,
    ) -> io::Result<Handshake<'a>> {
        let crypto = Crypto::new(
            v3::HANDSHAKE_CIPHER,
            config.password.as_ref(),
            config.password.as_ref(),
        )?;

        Ok(Handshake {
            config,
            server,
            req_addr,
            crypto,
        })
    }

    async fn hand(&mut self) -> io::Result<KeyPair> {
        let mut buf = [0u8; v3::MAX_BUFFER_SIZE];

        let request_len = self.request(&mut buf[..])?;

        self.server.write_all(&buf[..request_len]).await?;

        let resp_header_len = v3::DATA_LEN_LEN + self.crypto.tag_len();
        self.server.read_exact(&mut buf[..resp_header_len]).await?;
        let len = self.crypto.decrypt(&mut buf[..resp_header_len])?;

        debug_assert_eq!(len, v3::DATA_LEN_LEN);
        let data_len = ((buf[0] as usize) << 8) + (buf[1] as usize);
        // Padding len 1 + resp 1
        if data_len < 2 + self.crypto.tag_len() {
            return Err(other("response data length too small"));
        }

        self.server.read_exact(&mut buf[..data_len]).await?;
        let len = self.crypto.decrypt(&mut buf[..data_len])?;

        // +---------+-------+-------+------+
        // | PADDING |  RESP |  EKEY | DKEY |
        // +---------+-------+-------+------+
        // |   Var.  |    1  |  Var. | Var. |
        // +---------+-------+-------+------+
        let padding_len = (buf[0] as usize) + 1;
        if len < padding_len + 1 {
            return Err(other("response data length too small"));
        }

        match buf[padding_len] {
            v3::SERVER_RESP_SUCCEED => debug!("server resp success"),
            v3::SERVER_RESP_CIPHER_ERROR => return Err(other("server not support cipher")),
            v3::SERVER_RESP_ERROR => return Err(other("server internal error")),
            v3::SERVER_RESP_REMOTE_FAILED => {
                return Err(other("server connect request addr error"))
            }
            _ => return Err(other("server resp unknown")),
        }
        let key_len = len - padding_len - 1;
        if key_len != 2 * self.config.cipher.key_len() {
            return Err(other("server resp key length not match"));
        }

        Ok(KeyPair::from(&buf[padding_len + 1..len]))
    }

    // +---------------+------+--------------------------------------------------+
    // |     Header    | Plain|                      Data                        |
    // +-----+---------+------+---------+-----+------+-----+----------+----------+
    // | LEN | LEN TAG | USER | PADDING | VER | CTYP |ATYP | DST.ADDR | DST.PORT |
    // +-----+---------+------+---------+-----+------+----------------+----------+
    // |  2  |   Var.  |  32  |   Var.  |  1  |   1  |  1  |   Var.   |    2     |
    // +-----+---------+------+---------+-----+------+-----+----------+----------+
    fn request(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let tag_len = self.crypto.tag_len();
        let header_len = v3::DATA_LEN_LEN + tag_len;

        // USER
        let start = header_len;
        let end = header_len + v3::DEFAULT_DIGEST_LEN;
        (&mut buf[start..end]).copy_from_slice(self.config.username.as_ref());

        // PADDING
        let random_bytes = RandomBytes::new()?;
        let bytes = random_bytes.get();
        let start = end;
        let end = end + bytes.len();
        (&mut buf[start..end]).copy_from_slice(bytes);

        // VER and CTYP
        let start = end;
        let end = end + 2;

        buf[start] = v3::VERSION;
        buf[start + 1] = self.config.cipher.to_no();

        // ADDR
        let bytes = self.req_addr.get_bytes();
        let start = end;
        let end = end + bytes.len();
        (&mut buf[start..end]).copy_from_slice(bytes);

        // Encrypt it
        let data_len = end - header_len;
        let encrypted_data_len = data_len + tag_len;
        buf[0] = (encrypted_data_len >> 8) as u8;
        buf[1] = encrypted_data_len as u8;
        self.crypto.encrypt(&mut buf[0..header_len], 2)?;

        let len = data_len - v3::DEFAULT_DIGEST_LEN;
        let start = header_len + v3::DEFAULT_DIGEST_LEN;
        let end = end + tag_len;
        self.crypto.encrypt(&mut buf[start..end], len)?;

        Ok(end)
    }
}

#[inline]
fn other(desc: &str) -> io::Error {
    io::Error::new(io::ErrorKind::Other, desc)
}
