extern crate fakio;
#[macro_use]
extern crate log;

use std::env;
use std::process;

use fakio::{Server, ServerConfig};

fn main() {
    fakio::init_logger();

    let config_path = env::args()
        .nth(1)
        .unwrap_or_else(|| "conf/server.toml".to_string());
    let config = match ServerConfig::new(&config_path) {
        Ok(cfg) => cfg,
        Err(err) => {
            error!("load config {} failed, {}", config_path, err);
            process::exit(1);
        }
    };

    let server = Server::new(config);
    if let Err(e) = server.serve() {
        error!("start client failed, {}", e);
    }
}
