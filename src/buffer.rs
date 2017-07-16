use std::io::{self, Read, Write};

use futures::{Poll, Async};

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
    inner: Vec<u8>,
    curr: usize,
    state: BufState,
}

impl SharedBuf {
    pub fn new(size: usize) -> SharedBuf {
        SharedBuf {
            inner: vec![0u8; size],
            curr: 0,
            state: BufState::None,
        }
    }

    pub fn get_ref(&self) -> &[u8] {
        &self.inner[..]
    }

    pub fn get_mut(&mut self) -> &mut [u8] {
        &mut self.inner[..]
    }

    pub fn get_ref_from(&self, from: usize) -> &[u8] {
        assert!(from <= self.inner.len());
        &self.inner[from..]
    }

    pub fn get_mut_from(&mut self, from: usize) -> &mut [u8] {
        assert!(from <= self.inner.len());
        &mut self.inner[from..]
    }

    pub fn get_ref_to(&self, to: usize) -> &[u8] {
        assert!(to <= self.inner.len());
        &self.inner[0..to]
    }

    pub fn get_mut_to(&mut self, to: usize) -> &mut [u8] {
        assert!(to <= self.inner.len());
        &mut self.inner[0..to]
    }

    pub fn get_ref_range(&self, range: BufRange) -> &[u8] {
        let BufRange { start, end } = range;

        assert!(end <= self.inner.len());

        &self.inner[start..end]
    }

    pub fn get_mut_range(&mut self, range: BufRange) -> &mut [u8] {
        let BufRange { start, end } = range;

        assert!(end <= self.inner.len());

        &mut self.inner[start..end]
    }

    pub fn read_exact<R: Read>(
        &mut self,
        reader: &mut R,
        range: BufRange,
    ) -> Poll<BufRange, io::Error> {
        assert!(self.state != BufState::Writeing);

        self.state = BufState::Reading;

        let BufRange { start, end } = range;
        assert!(end <= self.inner.len());

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
        assert!(end <= self.inner.len());

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

fn eof() -> io::Error {
    io::Error::new(io::ErrorKind::UnexpectedEof, "early eof")
}

fn zero_write() -> io::Error {
    io::Error::new(io::ErrorKind::WriteZero, "zero-length write")
}
