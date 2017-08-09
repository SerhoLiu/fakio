#![allow(dead_code)]

use std::io::{self, Read, Write};

use futures::{Async, Poll};


#[derive(Copy, Clone, Debug)]
pub struct BufRange {
    pub start: usize,
    pub end: usize,
}

#[derive(Copy, PartialEq, Clone, Debug)]
enum BufState {
    Reading,
    Writeing,
    None,
}

pub struct SharedBuf {
    size: usize,
    inner: Vec<u8>,
    curr: usize,
    state: BufState,
}

impl SharedBuf {
    pub fn new(size: usize) -> SharedBuf {
        SharedBuf {
            size: size,
            inner: vec![0u8; size],
            curr: 0,
            state: BufState::None,
        }
    }

    #[inline]
    pub fn get_ref(&self) -> &[u8] {
        &self.inner[..]
    }

    #[inline]
    pub fn get_mut(&mut self) -> &mut [u8] {
        &mut self.inner[..]
    }

    #[inline]
    pub fn get_ref_from(&self, from: usize) -> &[u8] {
        assert!(from <= self.size);
        &self.inner[from..]
    }

    #[inline]
    pub fn get_mut_from(&mut self, from: usize) -> &mut [u8] {
        assert!(from <= self.size);
        &mut self.inner[from..]
    }

    #[inline]
    pub fn get_ref_to(&self, to: usize) -> &[u8] {
        assert!(to <= self.size);
        &self.inner[0..to]
    }

    #[inline]
    pub fn get_mut_to(&mut self, to: usize) -> &mut [u8] {
        assert!(to <= self.size);
        &mut self.inner[0..to]
    }

    #[inline]
    pub fn get_ref_range(&self, range: BufRange) -> &[u8] {
        let BufRange { start, end } = range;

        assert!(end <= self.size);

        &self.inner[start..end]
    }

    #[inline]
    pub fn get_mut_range(&mut self, range: BufRange) -> &mut [u8] {
        let BufRange { start, end } = range;

        assert!(end <= self.size);

        &mut self.inner[start..end]
    }

    #[inline]
    pub fn copy_from_slice(&mut self, range: BufRange, slice: &[u8]) {
        let BufRange { start, end } = range;
        assert!(end <= self.size);

        (&mut self.inner[start..end]).copy_from_slice(slice);
    }

    pub fn read_most<R: Read>(
        &mut self,
        reader: &mut R,
        range: BufRange,
    ) -> Poll<(bool, BufRange), io::Error> {
        assert!(self.state != BufState::Writeing);

        self.state = BufState::Reading;

        let BufRange { start, end } = range;
        assert!(end <= self.size);

        let mut eof = false;
        let mut curr = 0;
        let need = end - start;
        while curr < need {
            match reader.read(&mut self.inner[start + curr..end]) {
                Ok(n) => {
                    curr += n;
                    if n == 0 {
                        eof = true;
                        break;
                    }
                }
                Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                    break;
                }
                Err(e) => return Err(e),
            }
        }

        self.state = BufState::None;

        if !eof && curr == 0 {
            Ok(Async::NotReady)
        } else {
            Ok(Async::Ready((
                eof,
                BufRange {
                    start: start,
                    end: start + curr,
                },
            )))
        }
    }

    pub fn read_exact<R: Read>(
        &mut self,
        reader: &mut R,
        range: BufRange,
    ) -> Poll<BufRange, io::Error> {
        assert!(self.state != BufState::Writeing);

        self.state = BufState::Reading;

        let BufRange { start, end } = range;
        assert!(end <= self.size);

        let need = end - start;
        while self.curr < need {
            let n = try_nb!(reader.read(&mut self.inner[start + self.curr..end]));
            self.curr += n;
            if n == 0 {
                return Err(eof());
            }
        }

        self.curr = 0;
        self.state = BufState::None;
        Ok(Async::Ready(range))
    }

    pub fn write_exact<W: Write>(
        &mut self,
        writer: &mut W,
        range: BufRange,
    ) -> Poll<BufRange, io::Error> {
        assert!(self.state != BufState::Reading);

        self.state = BufState::Writeing;

        let BufRange { start, end } = range;
        assert!(end <= self.size);

        let need = end - start;
        while self.curr < need {
            let n = try_nb!(writer.write(&self.inner[start + self.curr..end]));
            self.curr += n;
            if n == 0 {
                return Err(zero_write());
            }
        }

        self.curr = 0;
        self.state = BufState::None;
        Ok(Async::Ready(range))
    }
}

#[inline]
fn eof() -> io::Error {
    io::Error::new(io::ErrorKind::UnexpectedEof, "early eof")
}

#[inline]
fn zero_write() -> io::Error {
    io::Error::new(io::ErrorKind::WriteZero, "zero-length write")
}
