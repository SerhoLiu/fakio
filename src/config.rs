use std::error;
use std::fs::File;
use std::net::{SocketAddr, ToSocketAddrs};
use std::io::Read;
use std::result;

use toml;
use serde::de;
use ring::digest::{digest, SHA256, SHA256_OUTPUT_LEN};
use crypto::Cipher;

pub type Result<T> = result::Result<T, Box<error::Error>>;


#[derive(Debug, Clone)]
pub struct ClientConfig {
    pub username: [u8; SHA256_OUTPUT_LEN],
    pub password: [u8; SHA256_OUTPUT_LEN],
    pub cipher: Cipher,
    pub server: SocketAddr,
    pub listen: SocketAddr,

    raw: TomlClientConfig,
}

impl ClientConfig {
    pub fn new(path: &str) -> Result<ClientConfig> {
        let raw: TomlClientConfig = read_toml_config(path)?;

        let cipher = match raw.cipher {
            Some(ref c) => Cipher::new(c).map_err(|err| format!("'{}' {}", c, err))?,
            None => Cipher::default(),
        };

        let server = raw.server
            .to_socket_addrs()
            .map(|mut addrs| addrs.nth(0).unwrap())
            .map_err(|err| format!("resolve server {}, {}", raw.server, err))?;

        let listen = raw.listen.parse::<SocketAddr>().map_err(|err| {
            format!("parse listen {}, {}", raw.listen, err)
        })?;

        let mut config = ClientConfig {
            username: [0u8; SHA256_OUTPUT_LEN],
            password: [0u8; SHA256_OUTPUT_LEN],
            cipher: cipher,
            server: server,
            listen: listen,
            raw: raw.clone(),
        };

        let digest_user = digest(&SHA256, raw.username.as_bytes());
        let digest_pass = digest(&SHA256, raw.password.as_bytes());
        config.username.copy_from_slice(digest_user.as_ref());
        config.password.copy_from_slice(digest_pass.as_ref());

        Ok(config)
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

fn read_toml_config<T>(path: &str) -> Result<T>
where
    T: de::DeserializeOwned,
{
    let mut file = File::open(path)?;

    let mut contents = String::new();
    file.read_to_string(&mut contents)?;

    let config = toml::from_str(&contents)?;
    Ok(config)
}
