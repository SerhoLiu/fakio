extern crate fakio;
#[macro_use]
extern crate log;

use std::env;
use std::process;

use fakio::{Client, ClientConfig};


fn main() {
    fakio::init_logger();

    let config_path = env::args()
        .nth(1)
        .unwrap_or_else(|| "conf/client.toml".to_string());
    let config = match ClientConfig::new(&config_path) {
        Ok(cfg) => cfg,
        Err(err) => {
            println!("load config {} failed, {}", config_path, err);
            process::exit(1);
        }
    };

    let client = Client::new(config);
    if let Err(e) = client.serve() {
        error!("start client failed, {}", e);
    }
}
