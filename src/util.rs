use std::borrow::Cow;
use std::env;
use std::fmt;
use std::io;
use std::path::MAIN_SEPARATOR;

use ansi_term::Color;
use env_logger::LogBuilder;
use log::{LogLevel, LogLevelFilter, LogRecord};
use ring::rand::{SecureRandom, SystemRandom};
use time;

use super::v3::MAX_PADDING_LEN;

struct ColorLevel(LogLevel);

impl fmt::Display for ColorLevel {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self.0 {
            LogLevel::Trace => Color::Purple.paint("TRACE"),
            LogLevel::Debug => Color::Blue.paint("DEBUG"),
            LogLevel::Info => Color::Green.paint("INFO "),
            LogLevel::Warn => Color::Yellow.paint("WARN "),
            LogLevel::Error => Color::Red.paint("ERROR"),
        }.fmt(f)
    }
}

pub fn init_logger() {
    let format = |record: &LogRecord| {
        let now = time::now();
        let ms = now.tm_nsec / 1000 / 1000;
        let t = time::strftime("%Y-%m-%d %T", &now).unwrap();
        format!(
            "{}.{:03} [{}]  {}",
            t,
            ms,
            ColorLevel(record.level()),
            record.args()
        )
    };

    let mut builder = LogBuilder::new();
    builder.format(format).filter(None, LogLevelFilter::Info);

    if env::var("RUST_LOG").is_ok() {
        builder.parse(&env::var("RUST_LOG").unwrap());
    }

    if env::var("FAKIO_LOG").is_ok() {
        builder.parse(&env::var("FAKIO_LOG").unwrap());
    }

    builder.init().unwrap();
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
            len: len,
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
    if path_after_tilde.is_empty() || path_after_tilde.starts_with('/')
        || path_after_tilde.starts_with(MAIN_SEPARATOR)
    {
        if let Some(hd) = env::home_dir() {
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
    use std::env;
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
        let old_home = env::var("HOME").ok();
        env::set_var("HOME", "/home/morty");

        assert_eq!("/home/morty", super::expand_tilde_path("~"));
        assert_eq!("/home/morty/rick", super::expand_tilde_path("~/rick"));
        assert_eq!("~rick", super::expand_tilde_path("~rick"));
        assert_eq!("/home", super::expand_tilde_path("/home"));

        if cfg!(windows) {
            env::set_var("HOME", r"C:\Users\Morty");
            assert_eq!(
                r"C:\Users\Morty\rick.txt",
                super::expand_tilde_path(r"~\rick.txt")
            );
        }

        if let Some(old) = old_home {
            env::set_var("HOME", old);
        }
    }
}
