use std::io;
use std::net::{SocketAddr, ToSocketAddrs};
use std::vec::IntoIter;

use futures::{Async, Future, Poll};
use futures_cpupool::{CpuFuture, CpuPool};
use tokio_core::net::{TcpStream, TcpStreamNew};
use tokio_core::reactor::Handle;


#[derive(Copy, Clone, Debug)]
enum State {
    AddrResolving,
    TcpConnecting,
    Done,
}

pub struct TcpConnect {
    handle: Handle,

    state: State,
    resolver: CpuFuture<Vec<SocketAddr>, io::Error>,
    current: Option<TcpStreamNew>,
    addrs: Option<IntoIter<SocketAddr>>,
}

impl TcpConnect {
    pub fn new(host: (String, u16), pool: CpuPool, handle: Handle) -> TcpConnect {
        TcpConnect {
            handle: handle,

            state: State::AddrResolving,
            resolver: pool.spawn_fn(move || match (&*host.0, host.1).to_socket_addrs() {
                Ok(addrs) => Ok(addrs.collect()),
                Err(e) => Err(e),
            }),
            current: None,
            addrs: None,
        }
    }

    fn try_connect(&mut self) -> Poll<TcpStream, io::Error> {
        let addrs = self.addrs.as_mut().unwrap();
        let mut err = None;

        loop {
            if let Some(ref mut current) = self.current {
                match current.poll() {
                    Ok(Async::Ready(conn)) => return Ok(Async::Ready(conn)),
                    Ok(Async::NotReady) => return Ok(Async::NotReady),
                    Err(e) => {
                        err = Some(e);
                        if let Some(addr) = addrs.next() {
                            *current = TcpStream::connect(&addr, &self.handle);
                            continue;
                        }
                    }
                }
            } else if let Some(addr) = addrs.next() {
                self.current = Some(TcpStream::connect(&addr, &self.handle));
                continue;
            }

            return Err(err.take().expect("missing connect error"));
        }
    }
}

impl Future for TcpConnect {
    type Item = TcpStream;
    type Error = io::Error;

    fn poll(&mut self) -> Poll<TcpStream, io::Error> {
        loop {
            match self.state {
                State::AddrResolving => {
                    let addrs = match self.resolver.poll()? {
                        Async::NotReady => return Ok(Async::NotReady),
                        Async::Ready(t) => t,
                    };
                    self.addrs = Some(addrs.into_iter());
                    self.state = State::TcpConnecting;
                }
                State::TcpConnecting => {
                    let conn = try_ready!(self.try_connect());
                    self.state = State::Done;
                    return Ok(Async::Ready(conn));
                }
                State::Done => panic!("poll done future"),
            }
        }
    }
}
