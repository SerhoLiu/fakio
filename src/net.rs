use std::io::{self, Read, Write};
use std::net::{Shutdown, SocketAddr, ToSocketAddrs};
use std::sync::Arc;
use std::vec::IntoIter;

use futures::{Async, Future, Poll};
use tokio::io::{AsyncRead, AsyncWrite};
use tokio::net::{ConnectFuture, TcpStream};
use tokio_threadpool::blocking;

#[cfg(
    any(
        target_os = "bitrig",
        target_os = "dragonfly",
        target_os = "freebsd",
        target_os = "ios",
        target_os = "macos",
        target_os = "netbsd",
        target_os = "openbsd"
    )
)]
use mio;

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

            return Err(err.unwrap_or_else(|| other_err("no socket addr for connect")));
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
                        Err(e) => return Err(other_err(&format!("tcp conncet, {}", e))),
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

#[derive(Clone, Debug)]
pub struct ProxyStream(Arc<TcpStream>);

impl ProxyStream {
    pub fn new(stream: TcpStream) -> ProxyStream {
        ProxyStream(Arc::new(stream))
    }
}

impl Read for ProxyStream {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let r = (&mut (&*self.0)).read(buf);

        if cfg!(any(
            target_os = "bitrig",
            target_os = "dragonfly",
            target_os = "freebsd",
            target_os = "ios",
            target_os = "macos",
            target_os = "netbsd",
            target_os = "openbsd"
        )) {
            match r {
                Ok(n) => Ok(n),
                Err(e) => {
                    // FIXME: https://github.com/tokio-rs/tokio/issues/449
                    // maybe tokio bug, maybe my bug...
                    if e.kind() == io::ErrorKind::WouldBlock {
                        if let Async::Ready(ready) =
                            self.0.poll_read_ready(mio::Ready::readable())?
                        {
                            if mio::unix::UnixReady::from(ready).is_hup() {
                                warn!("kqueue get EV_EOF, but read get WouldBlock, maybe a bug");
                                return Ok(0);
                            }
                        }
                    }
                    Err(e)
                }
            }
        } else {
            r
        }
    }
}

impl Write for ProxyStream {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        (&mut (&*self.0)).write(buf)
    }
    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

impl AsyncRead for ProxyStream {}

impl AsyncWrite for ProxyStream {
    fn shutdown(&mut self) -> Poll<(), io::Error> {
        self.0.shutdown(Shutdown::Write)?;
        Ok(().into())
    }
}
