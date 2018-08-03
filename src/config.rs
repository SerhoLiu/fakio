use std::borrow;
use std::collections::HashMap;
use std::convert;
use std::error;
use std::fs;
use std::net::{SocketAddr, ToSocketAddrs};
use std::result;

use ring::digest;
use serde::de;
use toml;

use super::crypto::Cipher;
use super::v3;

pub type Result<T> = result::Result<T, Box<error::Error>>;

#[derive(Debug, Hash, PartialEq, Eq, Copy, Clone)]
pub struct Digest {
    value: [u8; v3::DEFAULT_DIGEST_LEN],
}

#[derive(Debug, Clone)]
pub struct ClientConfig {
    pub username: Digest,
    pub password: Digest,
    pub cipher: Cipher,
    pub server: SocketAddr,
    pub listen: SocketAddr,
}

#[derive(Debug, Clone)]
pub struct User {
    pub name: String,
    pub password: Digest,
}

#[derive(Debug, Clone)]
pub struct ServerConfig {
    pub listen: SocketAddr,
    pub users: HashMap<Digest, User>,
}

impl Digest {
    fn new(value: &str) -> Digest {
        let mut d = Digest {
            value: [0u8; v3::DEFAULT_DIGEST_LEN],
        };

        d.value
            .copy_from_slice(digest::digest(v3::DEFAULT_DIGEST, value.as_bytes()).as_ref());
        d
    }

    #[inline]
    pub fn size(&self) -> usize {
        v3::DEFAULT_DIGEST_LEN
    }
}

impl convert::AsRef<[u8]> for Digest {
    fn as_ref(&self) -> &[u8] {
        &self.value
    }
}

impl borrow::Borrow<[u8]> for Digest {
    fn borrow(&self) -> &[u8] {
        &self.value
    }
}

impl ClientConfig {
    pub fn new(path: &str) -> Result<ClientConfig> {
        let raw: TomlClientConfig = read_toml_config(path)?;

        let cipher = match raw.cipher {
            Some(ref c) => Cipher::new(c).map_err(|err| format!("'{}' {}", c, err))?,
            None => Cipher::default(),
        };

        let server = raw
            .server
            .to_socket_addrs()
            .map(|mut addrs| addrs.nth(0).unwrap())
            .map_err(|err| format!("resolve server {}, {}", raw.server, err))?;

        let listen = raw
            .listen
            .parse::<SocketAddr>()
            .map_err(|err| format!("parse listen {}, {}", raw.listen, err))?;

        Ok(ClientConfig {
            username: Digest::new(&raw.username),
            password: Digest::new(&raw.password),
            cipher,
            server,
            listen,
        })
    }
}

impl ServerConfig {
    pub fn new(path: &str) -> Result<ServerConfig> {
        let raw: TomlServerConfig = read_toml_config(path)?;

        let listen = raw
            .server
            .listen
            .parse::<SocketAddr>()
            .map_err(|err| format!("parse listen {}, {}", raw.server.listen, err))?;

        let mut users = HashMap::new();

        for (user, password) in raw.users {
            users.insert(
                Digest::new(&user),
                User {
                    name: user,
                    password: Digest::new(&password),
                },
            );
        }

        Ok(ServerConfig { listen, users })
    }
}

#[derive(Debug, Clone, Deserialize)]
struct TomlClientConfig {
    username: String,
    password: String,
    cipher: Option<String>,
    server: String,
    listen: String,
}

#[derive(Debug, Deserialize)]
struct TomlServer {
    listen: String,
}

#[derive(Debug, Deserialize)]
struct TomlServerConfig {
    server: TomlServer,
    users: HashMap<String, String>,
}

fn read_toml_config<T>(path: &str) -> Result<T>
where
    T: de::DeserializeOwned,
{
    let content = fs::read_to_string(path)?;
    let config = toml::from_str(&content)?;
    Ok(config)
}
