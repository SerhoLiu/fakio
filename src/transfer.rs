use std::fmt;
use std::io;
use std::net::Shutdown;
use std::rc::Rc;

use futures::{future, Async, Flatten, Future, Poll};
use tokio_core::net::TcpStream;

use super::buffer::{BufRange, SharedBuf};
use super::crypto::{Cipher, Crypto};
use super::v3;

pub fn encrypt(
    reader: Rc<TcpStream>,
    writer: Rc<TcpStream>,
    cipher: Cipher,
    key: &[u8],
) -> Flatten<future::FutureResult<EncTransfer, io::Error>> {
    future::result(EncTransfer::new(reader, writer, cipher, key)).flatten()
}

pub fn decrypt(
    reader: Rc<TcpStream>,
    writer: Rc<TcpStream>,
    cipher: Cipher,
    key: &[u8],
) -> Flatten<future::FutureResult<DecTransfer, io::Error>> {
    future::result(DecTransfer::new(reader, writer, cipher, key)).flatten()
}

#[derive(Copy, Clone, Debug)]
enum EncState {
    Reading,
    Writing(BufRange),
    Shutdown,
    Done,
}

pub struct EncTransfer {
    reader: Rc<TcpStream>,
    writer: Rc<TcpStream>,
    read_eof: bool,
    nread: u64,
    nwrite: u64,

    state: EncState,
    crypto: Crypto,

    read_range: BufRange,
    buf: SharedBuf,
}

impl EncTransfer {
    fn new(
        reader: Rc<TcpStream>,
        writer: Rc<TcpStream>,
        cipher: Cipher,
        key: &[u8],
    ) -> io::Result<EncTransfer> {
        let crypto = Crypto::new(cipher, key, key)?;
        let read_range = BufRange {
            start: v3::DATA_LEN_LEN + crypto.tag_len(),
            end: v3::MAX_BUFFER_SIZE - crypto.tag_len(),
        };
        Ok(EncTransfer {
            reader: reader,
            writer: writer,
            read_eof: false,
            nread: 0,
            nwrite: 0,
            state: EncState::Reading,
            crypto: crypto,
            read_range: read_range,
            buf: SharedBuf::new(v3::MAX_BUFFER_SIZE),
        })
    }
}

impl Future for EncTransfer {
    type Item = (u64, u64);
    type Error = io::Error;

    fn poll(&mut self) -> Poll<(u64, u64), io::Error> {
        loop {
            match self.state {
                EncState::Reading => {
                    let (eof, range) =
                        try_ready!(self.buf.read_most(&mut (&*self.reader), self.read_range));
                    self.read_eof = eof;

                    let data_len = range.end - range.start;
                    self.nread += data_len as u64;

                    if eof && data_len == 0 {
                        // read done and no more data need write
                        self.state = EncState::Shutdown;
                    } else {
                        let tag_len = self.crypto.tag_len();
                        let data_range = BufRange {
                            start: range.start,
                            end: range.end + tag_len,
                        };

                        // Header
                        {
                            let range = BufRange {
                                start: 0,
                                end: range.start,
                            };
                            let head = self.buf.get_mut_range(range);

                            let data_len = data_len + tag_len;
                            // Big Endian
                            head[0] = (data_len >> 8) as u8;
                            head[1] = data_len as u8;
                            self.crypto.encrypt(head, 2)?;
                        }

                        // Data
                        {
                            let data = self.buf.get_mut_range(data_range);
                            self.crypto.encrypt(data, data_len)?;
                        }
                        self.state = EncState::Writing(BufRange {
                            start: 0,
                            end: data_range.end,
                        });
                    }
                }
                EncState::Writing(range) => {
                    try_ready!(self.buf.write_exact(&mut (&*self.writer), range));
                    self.nwrite += (range.end - range.start) as u64;

                    if self.read_eof {
                        self.state = EncState::Shutdown;
                    } else {
                        self.state = EncState::Reading;
                    }
                }
                EncState::Shutdown => {
                    try_nb!((&*self.writer).shutdown(Shutdown::Write));
                    self.state = EncState::Done;
                    return Ok(Async::Ready((self.nread, self.nwrite)));
                }
                EncState::Done => panic!("poll a done future"),
            }
        }
    }
}

#[derive(Copy, Clone, Debug)]
enum DecState {
    TryReadHead,
    ReadHead(BufRange),
    ReadData(BufRange),
    Writing(BufRange),
    Shutdown,
    Done,
}

pub struct DecTransfer {
    reader: Rc<TcpStream>,
    writer: Rc<TcpStream>,
    nread: u64,
    nwrite: u64,

    state: DecState,
    crypto: Crypto,

    head_range: BufRange,
    buf: SharedBuf,
}

impl DecTransfer {
    fn new(
        reader: Rc<TcpStream>,
        writer: Rc<TcpStream>,
        cipher: Cipher,
        key: &[u8],
    ) -> io::Result<DecTransfer> {
        let crypto = Crypto::new(cipher, key, key)?;
        let head_range = BufRange {
            start: 0,
            end: v3::DATA_LEN_LEN + crypto.tag_len(),
        };

        Ok(DecTransfer {
            reader: reader,
            writer: writer,
            nread: 0,
            nwrite: 0,
            state: DecState::TryReadHead,
            crypto: crypto,
            head_range: head_range,
            buf: SharedBuf::new(v3::MAX_BUFFER_SIZE),
        })
    }
}

impl Future for DecTransfer {
    type Item = (u64, u64);
    type Error = io::Error;

    fn poll(&mut self) -> Poll<(u64, u64), io::Error> {
        loop {
            match self.state {
                DecState::TryReadHead => {
                    let (eof, range) =
                        try_ready!(self.buf.read_most(&mut (&*self.reader), self.head_range));
                    let data_len = range.end - range.start;
                    if eof && data_len == 0 {
                        self.state = DecState::Shutdown;
                    } else {
                        self.state = DecState::ReadHead(BufRange {
                            start: range.end,
                            end: self.head_range.end,
                        })
                    }
                }
                DecState::ReadHead(range) => {
                    // try read is read all header
                    if range.start != range.end {
                        try_ready!(self.buf.read_exact(&mut (&*self.reader), range));
                    }
                    let range = self.head_range;

                    let buf = self.buf.get_mut_range(range);
                    let len = self.crypto.decrypt(buf)?;

                    // +-----+---------+
                    // | LEN | LEN TAG |
                    // +-----+---------+
                    assert_eq!(len, v3::DATA_LEN_LEN);
                    let data_len = ((buf[0] as usize) << 8) + (buf[1] as usize);

                    self.nread += (data_len + range.end) as u64;

                    self.state = DecState::ReadData(BufRange {
                        start: 0,
                        end: data_len,
                    });
                }
                DecState::ReadData(range) => {
                    let range = try_ready!(self.buf.read_exact(&mut (&*self.reader), range));
                    let len = {
                        let buf = self.buf.get_mut_range(range);
                        self.crypto.decrypt(buf)?
                    };
                    self.state = DecState::Writing(BufRange { start: 0, end: len })
                }
                DecState::Writing(range) => {
                    try_ready!(self.buf.write_exact(&mut (&*self.writer), range));
                    self.nwrite += (range.end - range.start) as u64;
                    self.state = DecState::TryReadHead;
                }
                DecState::Shutdown => {
                    try_nb!((&*self.writer).shutdown(Shutdown::Write));
                    self.state = DecState::Done;
                    return Ok(Async::Ready((self.nread, self.nwrite)));
                }
                DecState::Done => panic!("poll a done future"),
            }
        }
    }
}

#[derive(Copy, Clone, Debug)]
pub struct Stat {
    enc_read: u64,
    enc_write: u64,
    dec_read: u64,
    dec_write: u64,
}

impl Stat {
    pub fn new(enc: (u64, u64), dec: (u64, u64)) -> Stat {
        Stat {
            enc_read: enc.0,
            enc_write: enc.1,
            dec_read: dec.0,
            dec_write: dec.1,
        }
    }
}

impl fmt::Display for Stat {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "recv: {}/{} send: {}/{}",
            self.dec_write, self.dec_read, self.enc_read, self.enc_write
        )
    }
}
