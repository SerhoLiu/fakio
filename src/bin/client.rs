use std::env;
use std::process;

use log::error;

use fakio::{client, ClientConfig};

#[tokio::main]
async fn main() {
    fakio::init_logger();

    let config_path = env::args()
        .nth(1)
        .unwrap_or_else(|| "conf/client.toml".to_string());

    let config_path = fakio::expand_tilde_path(&config_path);
    let config = match ClientConfig::new(&*config_path) {
        Ok(cfg) => cfg,
        Err(err) => {
            error!("load config {} failed, {}", config_path, err);
            process::exit(1);
        }
    };

    if let Err(e) = client::serve(config).await {
        error!("start client failed, {}", e);
    }
}
