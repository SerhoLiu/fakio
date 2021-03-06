use std::borrow::Cow;
use std::env;
use std::fmt;
use std::io::{self, Write};
use std::path::MAIN_SEPARATOR;

use ansi_term::Color;
use dirs;
use env_logger::{Builder, Formatter};
use log::{Level, LevelFilter, Record};
use ring::rand::{SecureRandom, SystemRandom};
use time;

use super::v3::MAX_PADDING_LEN;

struct ColorLevel(Level);

impl fmt::Display for ColorLevel {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self.0 {
            Level::Trace => Color::Purple.paint("TRACE"),
            Level::Debug => Color::Blue.paint("DEBUG"),
            Level::Info => Color::Green.paint("INFO "),
            Level::Warn => Color::Yellow.paint("WARN "),
            Level::Error => Color::Red.paint("ERROR"),
        }
        .fmt(f)
    }
}

pub fn init_logger() {
    let format = |buf: &mut Formatter, record: &Record| {
        let now = time::now();
        let ms = now.tm_nsec / 1000 / 1000;
        let t = time::strftime("%Y-%m-%d %T", &now).unwrap();
        writeln!(
            buf,
            "{}.{:03} [{}]  {}",
            t,
            ms,
            ColorLevel(record.level()),
            record.args()
        )
    };

    let mut builder = Builder::new();
    builder.format(format).filter(None, LevelFilter::Info);

    if env::var("RUST_LOG").is_ok() {
        builder.parse(&env::var("RUST_LOG").unwrap());
    }

    if env::var("FAKIO_LOG").is_ok() {
        builder.parse(&env::var("FAKIO_LOG").unwrap());
    }

    builder.init();
}

/// `MAX_PADDING_LEN 255`
///
/// +-------+----------+
/// |  len  |   bytes  |
/// +-------+----------+
/// | 1bytes| [0, 255] |
/// +-------+----------+
pub struct RandomBytes {
    len: usize,
    bytes: [u8; 1 + MAX_PADDING_LEN],
}

impl RandomBytes {
    pub fn new() -> io::Result<RandomBytes> {
        let mut padding = [0u8; 1 + MAX_PADDING_LEN];
        let rand = SystemRandom::new();

        // 1. rand len
        rand.fill(&mut padding[..1])
            .map_err(|e| io::Error::new(io::ErrorKind::Other, format!("rand failed by {}", e)))?;

        // 2. rand data
        let len = padding[0] as usize;
        rand.fill(&mut padding[1..len + 1])
            .map_err(|e| io::Error::new(io::ErrorKind::Other, format!("rand failed by {}", e)))?;
        Ok(RandomBytes {
            len,
            bytes: padding,
        })
    }

    #[inline]
    pub fn get(&self) -> &[u8] {
        &self.bytes[..self.len + 1]
    }
}

static CHARS: &'static [u8] = b"0123456789abcdef";

pub fn to_hex(slice: &[u8]) -> String {
    let mut v = Vec::with_capacity(slice.len() * 2);
    for &byte in slice.iter() {
        v.push(CHARS[(byte >> 4) as usize]);
        v.push(CHARS[(byte & 0xf) as usize]);
    }

    unsafe { String::from_utf8_unchecked(v) }
}

/// Expand path like ~/xxx
pub fn expand_tilde_path(path: &str) -> Cow<str> {
    if !path.starts_with('~') {
        return path.into();
    }

    let path_after_tilde = &path[1..];

    // on support windows `\`
    if path_after_tilde.is_empty() || path_after_tilde.starts_with(MAIN_SEPARATOR) {
        if let Some(hd) = dirs::home_dir() {
            let result = format!("{}{}", hd.display(), path_after_tilde);
            result.into()
        } else {
            // home dir is not available
            path.into()
        }
    } else {
        // we cannot handle `~otheruser/` paths yet
        path.into()
    }
}

#[cfg(test)]
mod test {
    use std::io::ErrorKind;

    #[test]
    fn test_random_bytes() {
        match super::RandomBytes::new() {
            Ok(r) => {
                let bytes = r.get();
                let size = bytes[0] as usize;
                assert_eq!(size + 1, bytes.len());
            }
            Err(e) => assert_eq!(e.kind(), ErrorKind::Other),
        }
    }

    #[test]
    fn test_expand_tilde_path() {
        let mut home = match dirs::home_dir() {
            Some(hd) => hd,
            None => return,
        };

        assert_eq!(format!("{}", home.display()), super::expand_tilde_path("~"));
        assert_eq!("~rick", super::expand_tilde_path("~rick"));

        if cfg!(target_os = "windows") {
            home.push("rick");
            assert_eq!(
                format!("{}", home.display()),
                super::expand_tilde_path("~\\rick")
            );
            home.push("morty.txt");
            assert_eq!(
                format!("{}", home.display()),
                super::expand_tilde_path("~\\rick\\morty.txt")
            );
            assert_eq!("C:\\home", super::expand_tilde_path("C:\\home"));
        } else {
            home.push("rick");
            assert_eq!(
                format!("{}", home.display()),
                super::expand_tilde_path("~/rick")
            );
            home.push("morty.txt");
            assert_eq!(
                format!("{}", home.display()),
                super::expand_tilde_path("~/rick/morty.txt")
            );
            assert_eq!("/home", super::expand_tilde_path("/home"));
        }
    }
}
