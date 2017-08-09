#[macro_use]
extern crate log;
extern crate fakio;

use std::env;
use std::process;

use fakio::{config, server, util};


fn main() {
    util::init_logger();

    let config_path = env::args().nth(1).unwrap_or("conf/server.toml".to_string());
    let config = match config::ServerConfig::new(&config_path) {
        Ok(cfg) => cfg,
        Err(err) => {
            error!("load config {} failed, {}", config_path, err);
            process::exit(1);
        }
    };

    let server = server::Server::new(config);
    if let Err(e) = server.serve() {
        error!("start client failed, {}", e);
    }
}
