use std::fmt;
use std::io;
use std::marker::Unpin;

use tokio::io::{AsyncReadExt, AsyncWriteExt};

use super::crypto::{Cipher, Crypto};
use super::v3;

pub async fn encrypt<In: AsyncReadExt + Unpin, Out: AsyncWriteExt + Unpin>(
    mut reader: In,
    mut writer: Out,
    cipher: Cipher,
    key: &[u8],
) -> io::Result<(usize, usize)> {
    let mut nread = 0;
    let mut nwrite = 0;
    let mut crypto = Crypto::new(cipher, key, key)?;

    let crypto_tag_len = crypto.tag_len();

    let mut buf = [0u8; v3::MAX_BUFFER_SIZE];
    let start = v3::DATA_LEN_LEN + crypto_tag_len;
    let end = v3::MAX_BUFFER_SIZE - crypto_tag_len;

    loop {
        let n = reader.read(&mut buf[start..end]).await?;
        // read eof
        if n == 0 {
            writer.shutdown().await?;
            return Ok((nread, nwrite));
        }
        nread += n;

        // Header
        let data_len = n + crypto_tag_len;
        // Big Endian
        buf[0] = (data_len >> 8) as u8;
        buf[1] = data_len as u8;
        crypto.encrypt(&mut buf[..start], 2)?;

        // Data
        let data_end = start + n + crypto_tag_len;
        crypto.encrypt(&mut buf[start..data_end], n)?;

        writer.write_all(&buf[..data_end]).await?;
        nwrite += data_end;
    }
}

pub async fn decrypt<In: AsyncReadExt + Unpin, Out: AsyncWriteExt + Unpin>(
    mut reader: In,
    mut writer: Out,
    cipher: Cipher,
    key: &[u8],
) -> io::Result<(usize, usize)> {
    let mut nread = 0;
    let mut nwrite = 0;
    let mut crypto = Crypto::new(cipher, key, key)?;

    let crypto_tag_len = crypto.tag_len();

    let mut buf = [0u8; v3::MAX_BUFFER_SIZE];
    let header_len = v3::DATA_LEN_LEN + crypto_tag_len;

    loop {
        let n = reader.read(&mut buf[..header_len]).await?;
        // read eof
        if n == 0 {
            writer.shutdown().await?;
            return Ok((nread, nwrite));
        }
        nread += n;

        // try read all header
        if n != header_len {
            reader.read_exact(&mut buf[n..header_len]).await?;
            nread += header_len - n;
        }

        let len = crypto.decrypt(&mut buf[..header_len])?;
        assert_eq!(len, v3::DATA_LEN_LEN);
        let data_len = ((buf[0] as usize) << 8) + (buf[1] as usize);

        // read data
        reader.read_exact(&mut buf[..data_len]).await?;
        nread += data_len;
        let len = crypto.decrypt(&mut buf[..data_len])?;

        writer.write_all(&buf[..len]).await?;
        nwrite += len;
    }
}

#[derive(Copy, Clone, Debug)]
pub struct Stat {
    enc_read: usize,
    enc_write: usize,
    dec_read: usize,
    dec_write: usize,
}

impl Stat {
    pub fn new(enc: (usize, usize), dec: (usize, usize)) -> Stat {
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
