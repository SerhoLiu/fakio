use std::io;
use std::rc::Rc;

use futures::{future, Future, Stream};
use tokio_core::reactor;
use tokio_core::net::{TcpStream, TcpListener};

use socks5;
use config::ClientConfig;

pub struct Client {
    config: Rc<ClientConfig>,
    socks5_reply: Rc<socks5::Reply>,
}

impl Client {
    pub fn new(config: ClientConfig) -> Client {
        let reply = socks5::Reply::new(config.listen);
        Client {
            config: Rc::new(config),
            socks5_reply: Rc::new(reply),
        }
    }

    pub fn serve(&self) -> io::Result<()> {
        let mut core = reactor::Core::new()?;
        let handle = core.handle();

        let listener = TcpListener::bind(&self.config.listen, &handle).map_err(
            |e| {
                other(&format!("bind on {}, {}", self.config.listen, e))
            },
        )?;

        info!("Listening for socks5 proxy on local {}", self.config.listen);

        let clients = listener.incoming().map(|(conn, addr)| {
            (
                Local {
                    config: self.config.clone(),
                    handle: handle.clone(),
                    client: Rc::new(conn),
                    socks5_reply: self.socks5_reply.clone(),
                }.serve(),
                addr,
            )
        });

        let server = clients.for_each(|(client, addr)| {
            handle.spawn(client.then(move |res| {
                match res {
                    Ok(()) => {
                        println!("proxied for {}", addr)
                    }
                    Err(e) => println!("error for {}: {}", addr, e),
                }
                future::ok(())
            }));
            Ok(())
        });

        core.run(server).unwrap();
        Ok(())
    }
}

struct Local {
    config: Rc<ClientConfig>,
    socks5_reply: Rc<socks5::Reply>,
    handle: reactor::Handle,

    client: Rc<TcpStream>,
}

impl Local {
    fn serve(self) -> Box<Future<Item = (), Error = io::Error>> {
        Box::new(
            socks5::Handshake::new(
                self.handle.clone(),
                self.socks5_reply,
                self.client,
                self.config.server,
            ).and_then(|conn| conn.set_nodelay(true)),
        )
    }
}


fn other(desc: &str) -> io::Error {
    io::Error::new(io::ErrorKind::Other, desc)
}
