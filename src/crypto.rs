use std::fmt;
use std::io;
use std::result;

use ring::aead::BoundKey;
use ring::error::Unspecified;
use ring::hkdf::KeyType;
use ring::rand::{SecureRandom, SystemRandom};
use ring::{aead, digest, hkdf};

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

#[derive(Clone, Copy, Debug, Default)]
pub enum Cipher {
    #[default]
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

    pub fn to_no(self) -> u8 {
        match self {
            Cipher::AES128GCM => 1,
            Cipher::AES256GCM => 2,
            Cipher::CHACHA20POLY1305 => 3,
        }
    }

    #[inline]
    pub fn key_len(self) -> usize {
        self.algorithm().key_len()
    }

    #[inline]
    pub fn tag_len(self) -> usize {
        self.algorithm().tag_len()
    }

    fn algorithm(self) -> &'static aead::Algorithm {
        match self {
            Cipher::AES128GCM => &aead::AES_128_GCM,
            Cipher::AES256GCM => &aead::AES_256_GCM,
            Cipher::CHACHA20POLY1305 => &aead::CHACHA20_POLY1305,
        }
    }
}

const INFO_KEY: &str = "hello kelsi";

#[derive(Debug)]
pub struct KeyPair {
    value: Vec<u8>,
}

struct KeyPairCipher(Cipher);

impl KeyType for KeyPairCipher {
    fn len(&self) -> usize {
        self.0.key_len() * 2
    }
}

impl KeyPair {
    pub fn generate(secret: &[u8], cipher: Cipher) -> Result<KeyPair> {
        let mut bytes = [0u8; digest::MAX_OUTPUT_LEN];
        let rng = SystemRandom::new();
        rng.fill(&mut bytes[..hkdf::HKDF_SHA256.len()])
            .map_err(|_| Error::GenKey)?;
        let salt = hkdf::Salt::new(hkdf::HKDF_SHA256, &bytes[..hkdf::HKDF_SHA256.len()]);

        let keypair_cipher = KeyPairCipher(cipher);
        let mut out = vec![0u8; keypair_cipher.len()];

        let prk = salt.extract(secret);
        let info = &[INFO_KEY.as_bytes()];
        prk.expand(info, keypair_cipher)
            .and_then(|okm| okm.fill(&mut out[..]))
            .map_err(|_| Error::GenKey)?;

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

    open_key: aead::OpeningKey<NonceSequence>,
    seal_key: aead::SealingKey<NonceSequence>,
}

impl Crypto {
    pub fn new(cipher: Cipher, open_key: &[u8], seal_key: &[u8]) -> Result<Crypto> {
        let aead = cipher.algorithm();
        let key_len = aead.key_len();

        if open_key.len() != key_len {
            return Err(Error::KeyLenNotMatch(key_len));
        }
        let open_uk = aead::UnboundKey::new(aead, open_key).map_err(|_| Error::OpenKey)?;
        let open_key = aead::OpeningKey::new(open_uk, NonceSequence::new());

        if seal_key.len() != key_len {
            return Err(Error::KeyLenNotMatch(key_len));
        }
        let seal_uk = aead::UnboundKey::new(aead, seal_key).map_err(|_| Error::SealKey)?;
        let seal_key = aead::SealingKey::new(seal_uk, NonceSequence::new());

        Ok(Crypto {
            cipher,
            aead,

            tag_len: aead.tag_len(),
            key_len: aead.key_len(),
            nonce_len: aead.nonce_len(),

            open_key,
            seal_key,
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

        match self
            .seal_key
            .seal_in_place_separate_tag(aead::Aad::empty(), &mut inout[..in_len])
        {
            Ok(tag) => {
                inout[in_len..out_len].copy_from_slice(tag.as_ref());
                Ok(out_len)
            }
            Err(_) => Err(Error::Seal),
        }
    }

    #[inline]
    pub fn decrypt(&mut self, inout: &mut [u8]) -> Result<usize> {
        match self.open_key.open_in_place(aead::Aad::empty(), inout) {
            Ok(buf) => Ok(buf.len()),
            Err(_) => Err(Error::Open),
        }
    }
}

struct NonceSequence([u8; aead::NONCE_LEN]);

impl NonceSequence {
    const fn new() -> NonceSequence {
        NonceSequence([0u8; aead::NONCE_LEN])
    }
}

impl aead::NonceSequence for NonceSequence {
    fn advance(&mut self) -> result::Result<aead::Nonce, Unspecified> {
        let nonce = aead::Nonce::assume_unique_for_key(self.0);

        let mut overflow = false;
        for byte in self.0.iter_mut() {
            let (sum, of) = (*byte).overflowing_add(1);
            *byte = sum;
            overflow = of;
            if !overflow {
                break;
            }
        }

        if overflow {
            Err(Unspecified)
        } else {
            Ok(nonce)
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
        io::Error::other(format!("{}", err))
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

    use ring::aead::NonceSequence;

    #[test]
    fn test_incr_nonce() {
        let mut nonce_seq = super::NonceSequence::new();
        for i in 0..1024 {
            let nonce = nonce_seq.advance().unwrap();
            let x = (nonce.as_ref()[0] as usize)
                + ((nonce.as_ref()[1] as usize) << 8)
                + ((nonce.as_ref()[2] as usize) << 16)
                + ((nonce.as_ref()[3] as usize) << 24);
            assert_eq!(x, i);
        }
    }
}
