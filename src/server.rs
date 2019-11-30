use std::collections::HashMap;
use std::io;
use std::mem;
use std::net::SocketAddr;
use std::sync::Arc;

use futures::future::try_join;
use rand;
use tokio;
use tokio::net::{TcpListener, TcpStream};
use tokio::prelude::*;
use tokio::time::{timeout, Duration};

use super::config::{Digest, ServerConfig, User};
use super::crypto::{Cipher, Crypto, KeyPair};
use super::socks5;
use super::transfer::{self, Stat};
use super::util::{self, RandomBytes};
use super::v3;

pub async fn serve(config: ServerConfig) -> io::Result<()> {
    let mut listener = TcpListener::bind(&config.listen)
        .await
        .map_err(|e| other(&format!("listen on {}, {}", config.listen, e)))?;

    info!("Listening on {}", config.listen);
    let users = Arc::new(config.users);

    loop {
        match listener.accept().await {
            Ok((client, client_addr)) => {
                let users = users.clone();

                tokio::spawn(async move {
                    match process(client, client_addr, users).await {
                        Ok((user, req, stat)) => info!(
                            "{} - ({}) request {} success, {}",
                            client_addr, user.name, req, stat,
                        ),
                        Err(e) => error!("{} - failed by {}", client_addr, e),
                    }
                });
            }
            Err(e) => error!("accepting socket, {}", e),
        }
    }
}

async fn process(
    mut client: TcpStream,
    client_addr: SocketAddr,
    users: Arc<HashMap<Digest, User>>,
) -> io::Result<(User, String, Stat)> {
    // timeout random [10, 40)
    let secs = u64::from(rand::random::<u8>() % 30 + 10);

    let mut handshake = Handshake::new(&mut client, client_addr, users);
    let mut ctx = timeout(Duration::from_secs(secs), handshake.hand()).await??;

    let (client_key, server_key) = ctx.key_pair.split();
    let (cr, cw) = client.split();
    let (rr, rw) = ctx.remote.split();

    let en = transfer::encrypt(rr, cw, ctx.cipher, server_key);
    let de = transfer::decrypt(cr, rw, ctx.cipher, client_key);

    let stat = try_join(en, de).await?;
    Ok((ctx.user, ctx.addr, Stat::new(stat.0, stat.1)))
}

struct Context {
    user: User,
    addr: String,
    cipher: Cipher,
    key_pair: KeyPair,
    remote: TcpStream,
}

struct Request {
    addr: String,
    user: Option<User>,
    crypto: Option<Crypto>,

    remote: Option<TcpStream>,
    cipher: Option<Cipher>,
    key_pair: Option<KeyPair>,
}

impl Request {
    pub fn none() -> Request {
        Request {
            addr: String::new(),
            user: None,
            crypto: None,
            remote: None,
            cipher: None,
            key_pair: None,
        }
    }
}

struct Handshake<'a> {
    client: &'a mut TcpStream,
    client_addr: SocketAddr,
    users: Arc<HashMap<Digest, User>>,

    req: Request,
}

impl<'a> Handshake<'a> {
    fn new(
        client: &'a mut TcpStream,
        client_addr: SocketAddr,
        users: Arc<HashMap<Digest, User>>,
    ) -> Handshake {
        Handshake {
            client,
            client_addr,
            users,
            req: Request::none(),
        }
    }

    // +-----+---------+------+
    // | LEN | LEN TAG | USER |
    // +-----+---------+------+
    // |  2  |   Var.  |  32  |
    // +-----+---------+------+
    fn parse_header(&mut self, header: &mut [u8]) -> io::Result<usize> {
        let user = {
            let end = header.len();
            let start = end - v3::DEFAULT_DIGEST_LEN;
            let user = &header[start..end];
            self.users
                .get(user)
                .ok_or_else(|| other(&format!("user ({}) not exists", util::to_hex(user))))?
        };

        let mut crypto = Crypto::new(
            v3::HANDSHAKE_CIPHER,
            user.password.as_ref(),
            user.password.as_ref(),
        )?;

        let size = crypto.decrypt(&mut header[..v3::DATA_LEN_LEN + crypto.tag_len()])?;
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
    fn parse_data(&mut self, data: &mut [u8]) -> io::Result<Option<(String, u16)>> {
        let crypto = self.req.crypto.as_mut().unwrap();
        let user = self.req.user.as_ref().unwrap();

        let data_len = crypto.decrypt(data)?;

        let padding_len = (data[0] as usize) + 1;
        if data_len < padding_len + 1 + 1 + 1 + 1 {
            return Err(other(&format!(
                "({}) request data length not match",
                user.name
            )));
        }

        let buf = &data[padding_len..data_len];
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
                    "{} - ({}) request {}, cipher {}",
                    self.client_addr, user.name, self.req.addr, cipher,
                );
                Ok(Some(addr))
            }
            Err(_) => {
                info!(
                    "{} - ({}) request {}, cipher '{}' not support",
                    self.client_addr, user.name, self.req.addr, cipher_no,
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
    fn response(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let user = self.req.user.as_ref().unwrap();
        let crypto = self.req.crypto.as_mut().unwrap();

        let tag_len = crypto.tag_len();
        let header_len = v3::DATA_LEN_LEN + tag_len;
        let mut data_len = 0;

        // PADDING
        let random_bytes = RandomBytes::new()?;
        let bytes = random_bytes.get();
        let mut end = header_len + bytes.len();

        (&mut buf[header_len..end]).copy_from_slice(bytes);
        data_len += bytes.len();

        let mut resp = v3::SERVER_RESP_SUCCEED;

        self.req.key_pair = match self.req.cipher {
            Some(cipher) => match KeyPair::generate(user.password.as_ref(), cipher) {
                Ok(key) => Some(key),
                Err(e) => {
                    error!(
                        "{} - ({}) request generate key, {}",
                        self.client_addr, user.name, e,
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
        if self.req.key_pair.is_some() && self.req.remote.is_none() {
            resp = v3::SERVER_RESP_REMOTE_FAILED;
        }

        buf[end] = resp;
        end += 1;
        data_len += 1;

        // KEY
        if resp == v3::SERVER_RESP_SUCCEED {
            let key = self.req.key_pair.as_ref().unwrap();
            (&mut buf[end..end + key.len()]).copy_from_slice(key.as_ref());
            data_len += key.len();
        }

        // Big Endian
        let data_len = data_len + tag_len;
        buf[0] = (data_len >> 8) as u8;
        buf[1] = data_len as u8;
        crypto.encrypt(&mut buf[..header_len], 2)?;
        crypto.encrypt(
            &mut buf[header_len..header_len + data_len],
            data_len - tag_len,
        )?;

        Ok(header_len + data_len)
    }

    async fn hand(&mut self) -> io::Result<Context> {
        let mut buf = [0u8; v3::MAX_BUFFER_SIZE];
        let header_len = v3::DATA_LEN_LEN + v3::HANDSHAKE_CIPHER.tag_len() + v3::DEFAULT_DIGEST_LEN;

        self.client.read_exact(&mut buf[..header_len]).await?;
        let need = self.parse_header(&mut buf[..header_len])?;
        self.client.read_exact(&mut buf[..need]).await?;

        if let Some(addr) = self.parse_data(&mut buf[..need])? {
            match TcpStream::connect((&*addr.0, addr.1)).await {
                Ok(conn) => self.req.remote = Some(conn),
                Err(e) => {
                    let user = self.req.user.as_ref().unwrap();
                    error!(
                        "{} - ({}) connect remote {}, {}",
                        self.client_addr, user.name, self.req.addr, e,
                    )
                }
            }
        }

        let len = self.response(&mut buf)?;
        self.client.write_all(&buf[..len]).await?;

        let req = mem::replace(&mut self.req, Request::none());
        let user = req.user.unwrap();
        let addr = req.addr;

        let remote = match req.remote {
            Some(conn) => conn,
            None => return Err(other(&format!("({}) request {} failed", user.name, addr))),
        };
        Ok(Context {
            user,
            addr,
            cipher: req.cipher.unwrap(),
            key_pair: req.key_pair.unwrap(),
            remote,
        })
    }
}

fn other(desc: &str) -> io::Error {
    io::Error::new(io::ErrorKind::Other, desc)
}
