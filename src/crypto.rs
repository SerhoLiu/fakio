use std::fmt;
use std::io;
use std::result;

use ring::{aead, digest, hkdf, hmac, rand};

pub type Result<T> = result::Result<T, Error>;

#[derive(Clone, Copy, Debug)]
pub enum Error {
    CipherNotSupport,
    GenKey,
    KeyLenNotMatch(usize),
    OpenKey,
    SealKey,
    SealBufferTooSmall(usize),
    Open,
    Seal,
}

#[derive(Clone, Copy, Debug)]
pub enum Cipher {
    AES128GCM,
    AES256GCM,
    CHACHA20POLY1305,
}

impl Cipher {
    pub fn new(name: &str) -> Result<Cipher> {
        match name.to_lowercase().as_ref() {
            "aes-128-gcm" => Ok(Cipher::AES128GCM),
            "aes-256-gcm" => Ok(Cipher::AES256GCM),
            "chacha20-poly1305" => Ok(Cipher::CHACHA20POLY1305),
            _ => Err(Error::CipherNotSupport),
        }
    }

    pub fn from_no(no: u8) -> Result<Cipher> {
        match no {
            1 => Ok(Cipher::AES128GCM),
            2 => Ok(Cipher::AES256GCM),
            3 => Ok(Cipher::CHACHA20POLY1305),
            _ => Err(Error::CipherNotSupport),
        }
    }

    pub fn to_no(&self) -> u8 {
        match *self {
            Cipher::AES128GCM => 1,
            Cipher::AES256GCM => 2,
            Cipher::CHACHA20POLY1305 => 3,

        }
    }

    #[inline]
    pub fn key_len(&self) -> usize {
        self.algorithm().key_len()
    }

    #[inline]
    pub fn tag_len(&self) -> usize {
        self.algorithm().tag_len()
    }

    fn algorithm(&self) -> &'static aead::Algorithm {
        match *self {
            Cipher::AES128GCM => &aead::AES_128_GCM,
            Cipher::AES256GCM => &aead::AES_256_GCM,
            Cipher::CHACHA20POLY1305 => &aead::CHACHA20_POLY1305,

        }
    }
}

impl Default for Cipher {
    fn default() -> Cipher {
        Cipher::AES128GCM
    }
}


const INFO_KEY: &'static str = "hello kelsi";


#[derive(Debug)]
pub struct KeyPair {
    value: Vec<u8>,
}

impl KeyPair {
    pub fn generate(secret: &[u8], cipher: Cipher) -> Result<KeyPair> {
        let len = cipher.key_len() * 2;

        let salt = hmac::SigningKey::generate(&digest::SHA256, &rand::SystemRandom::new())
            .map_err(|_| Error::GenKey)?;

        let mut out = Vec::with_capacity(len);

        // not need init it
        unsafe {
            out.set_len(len);
        }
        hkdf::extract_and_expand(&salt, secret, INFO_KEY.as_bytes(), &mut out);
        Ok(KeyPair { value: out })
    }

    pub fn from(slice: &[u8]) -> KeyPair {
        let mut key = Vec::with_capacity(slice.len());

        key.extend_from_slice(slice);
        KeyPair { value: key }
    }

    pub fn len(&self) -> usize {
        self.value.len()
    }

    pub fn split(&self) -> (&[u8], &[u8]) {
        let len = self.value.len() / 2;
        (&self.value[..len], &self.value[len..])
    }
}

impl AsRef<[u8]> for KeyPair {
    fn as_ref(&self) -> &[u8] {
        self.value.as_ref()
    }
}


#[allow(dead_code)]
pub struct Crypto {
    cipher: Cipher,
    aead: &'static aead::Algorithm,

    tag_len: usize,
    key_len: usize,
    nonce_len: usize,

    open_key: aead::OpeningKey,
    open_nonce: Vec<u8>,

    seal_key: aead::SealingKey,
    seal_nonce: Vec<u8>,
}

impl Crypto {
    pub fn new(cipher: Cipher, open_key: &[u8], seal_key: &[u8]) -> Result<Crypto> {
        let aead = cipher.algorithm();
        let key_len = aead.key_len();

        if open_key.len() != key_len {
            return Err(Error::KeyLenNotMatch(key_len));
        }
        let open_key = aead::OpeningKey::new(aead, open_key)
            .map_err(|_| Error::OpenKey)?;

        if seal_key.len() != key_len {
            return Err(Error::KeyLenNotMatch(key_len));
        }
        let seal_key = aead::SealingKey::new(aead, seal_key)
            .map_err(|_| Error::SealKey)?;

        let nonce_len = aead.nonce_len();

        Ok(Crypto {
            cipher: cipher,
            aead: aead,

            tag_len: aead.tag_len(),
            key_len: aead.key_len(),
            nonce_len: aead.nonce_len(),

            open_key: open_key,
            open_nonce: vec![0u8; nonce_len],
            seal_key: seal_key,
            seal_nonce: vec![0u8; nonce_len],
        })
    }

    #[inline]
    pub fn tag_len(&self) -> usize {
        self.tag_len
    }

    pub fn encrypt(&mut self, inout: &mut [u8], in_len: usize) -> Result<usize> {
        let out_len = in_len + self.tag_len;
        if inout.len() < out_len {
            return Err(Error::SealBufferTooSmall(out_len));
        }

        match aead::seal_in_place(
            &self.seal_key,
            &self.seal_nonce,
            &[],
            &mut inout[..out_len],
            self.tag_len,
        ) {
            Ok(outlen) => debug_assert!(out_len == outlen),
            Err(_) => return Err(Error::Seal),
        };

        incr_nonce(&mut self.seal_nonce);

        Ok(out_len)
    }

    #[inline]
    pub fn decrypt(&mut self, inout: &mut [u8]) -> Result<usize> {
        match aead::open_in_place(&self.open_key, &self.open_nonce, &[], 0, inout) {
            Ok(buf) => {
                incr_nonce(&mut self.open_nonce);
                Ok(buf.len())
            }
            Err(_) => Err(Error::Open),
        }
    }
}

fn incr_nonce(nonce: &mut [u8]) {
    for byte in nonce.iter_mut() {
        let (sum, overflow) = (*byte).overflowing_add(1);
        *byte = sum;
        if !overflow {
            break;
        }
    }
}

impl fmt::Display for Error {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            Error::CipherNotSupport => write!(fmt, "cipher not support"),
            Error::GenKey => write!(fmt, "generate key error"),
            Error::KeyLenNotMatch(need) => write!(fmt, "key length not match, need {}", need),
            Error::OpenKey => write!(fmt, "ring open key error"),
            Error::SealKey => write!(fmt, "ring seal key error"),
            Error::SealBufferTooSmall(need) => {
                write!(fmt, "seal inout buffer too small, need {}", need)
            }
            Error::Open => write!(fmt, "ring open error"),
            Error::Seal => write!(fmt, "ring seal error"),
        }
    }
}

impl From<Error> for io::Error {
    fn from(err: Error) -> io::Error {
        io::Error::new(io::ErrorKind::Other, format!("{}", err))
    }
}

impl fmt::Display for Cipher {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            Cipher::AES128GCM => write!(fmt, "AES-128-GCM"),
            Cipher::AES256GCM => write!(fmt, "AES-256-GCM"),
            Cipher::CHACHA20POLY1305 => write!(fmt, "CHACHA20-POLY1305"),
        }
    }
}


#[cfg(test)]
mod test {

    #[test]
    fn test_incr_nonce() {
        let mut nonce = [0u8; 4];
        for i in 1..1024 {
            super::incr_nonce(&mut nonce);
            let x = (nonce[0] as usize) + ((nonce[1] as usize) << 8) +
                ((nonce[2] as usize) << 16) + ((nonce[3] as usize) << 24);
            assert!(x == i);
        }
    }
}
