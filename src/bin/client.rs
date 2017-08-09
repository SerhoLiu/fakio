#[macro_use]
extern crate log;
extern crate fakio;

use std::env;
use std::process;

use fakio::{client, config, util};


fn main() {
    util::init_logger();

    let config_path = env::args().nth(1).unwrap_or("conf/client.toml".to_string());
    let config = match config::ClientConfig::new(&config_path) {
        Ok(cfg) => cfg,
        Err(err) => {
            println!("load config {} failed, {}", config_path, err);
            process::exit(1);
        }
    };

    let client = client::Client::new(config);
    if let Err(e) = client.serve() {
        error!("start client failed, {}", e);
    }
}
