use std::io;
use std::net::{SocketAddr, ToSocketAddrs};
use std::vec::IntoIter;

use futures::{Async, Future, Poll};
use tokio::net::{ConnectFuture, TcpStream};
use tokio_threadpool::blocking;

#[derive(Copy, Clone, Debug)]
enum State {
    AddrResolving,
    TcpConnecting,
    Done,
}

pub struct TcpConnect {
    state: State,
    addr: (String, u16),
    current: Option<ConnectFuture>,
    addrs: Option<IntoIter<SocketAddr>>,
}

impl TcpConnect {
    pub fn new(addr: (String, u16)) -> TcpConnect {
        TcpConnect {
            state: State::AddrResolving,
            addr,
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
                            *current = TcpStream::connect(&addr);
                            continue;
                        }
                    }
                }
            } else if let Some(addr) = addrs.next() {
                self.current = Some(TcpStream::connect(&addr));
                continue;
            }

            return Err(err.unwrap_or(other_err("no socket addr for connect")));
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
                    let addrs = match blocking(|| (&*self.addr.0, self.addr.1).to_socket_addrs()) {
                        Ok(Async::Ready(Ok(v))) => v,
                        Ok(Async::Ready(Err(err))) => return Err(err),
                        Ok(Async::NotReady) => return Ok(Async::NotReady),
                        Err(e) => return Err(other_err(&format!("{:}", e))),
                    };

                    self.addrs = Some(addrs);
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

#[inline]
fn other_err(desc: &str) -> io::Error {
    io::Error::new(io::ErrorKind::Other, desc)
}
