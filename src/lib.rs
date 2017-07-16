extern crate ring;
extern crate toml;
extern crate serde;
#[macro_use]
extern crate serde_derive;
#[macro_use]
extern crate futures;
#[macro_use]
extern crate tokio_io;
extern crate tokio_core;
#[macro_use]
extern crate log;
extern crate env_logger;
extern crate time;
extern crate ansi_term;

mod buffer;
mod crypto;
mod socks5;
mod transfer;

pub mod config;
pub mod client;
pub mod util;

pub const VERSION: u8 = 0x03;
