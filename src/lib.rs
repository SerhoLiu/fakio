#[macro_use]
extern crate log;
extern crate time;
extern crate rand;
extern crate ring;
extern crate toml;
extern crate serde;
extern crate ansi_term;
extern crate env_logger;
#[macro_use]
extern crate serde_derive;
#[macro_use]
extern crate futures;
extern crate futures_cpupool;
#[macro_use]
extern crate tokio_io;
extern crate tokio_core;

mod net;
mod buffer;
mod crypto;
mod socks5;
mod transfer;

pub mod config;
pub mod client;
pub mod server;
pub mod util;

mod v3 {
    use ring::digest::{Algorithm, SHA256, SHA256_OUTPUT_LEN};

    use crypto::Cipher;

    pub const VERSION: u8 = 0x03;

    pub const MAX_BUFFER_SIZE: usize = 32 * 1024;

    pub const DATA_LEN_LEN: usize = 2;
    pub const MAX_PADDING_LEN: usize = 255;

    pub static DEFAULT_DIGEST: &'static Algorithm = &SHA256;
    pub const DEFAULT_DIGEST_LEN: usize = SHA256_OUTPUT_LEN;

    pub const HANDSHAKE_CIPHER: Cipher = Cipher::AES256GCM;

    pub const SERVER_RESP_SUCCESSD: u8 = 0x00;
    pub const SERVER_RESP_CIPHER_ERROR: u8 = 0x01;
    pub const SERVER_RESP_ERROR: u8 = 0x02;
    pub const SERVER_RESP_REMOTE_FAILED: u8 = 0x03;
}
