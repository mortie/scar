use crate::util::ReadSeek;
use std::cmp::{max, min};
use std::io::{self, Read, Write};
use anyhow::{Result, anyhow};

pub mod gzip;
pub use self::gzip::{GzipCompressorFactory, GzipDecompressorFactory};

pub mod xz;
pub use self::xz::{XzCompressorFactory, XzDecompressorFactory};

pub mod zstd;
pub use self::zstd::{ZstdCompressorFactory, ZstdDecompressorFactory};

pub mod plain;
pub use self::plain::{PlainCompressorFactory, PlainDecompressorFactory};

pub trait Compressor: Write {
    fn finish(self: Box<Self>) -> io::Result<Box<dyn Write>>;
}

pub trait Decompressor: Read {
    fn finish(self: Box<Self>) -> Box<dyn Read>;
}

pub trait CompressorFactory {
    fn create_compressor(&self, w: Box<dyn Write>) -> io::Result<Box<dyn Compressor>>;
    fn eof_marker(&self) -> &'static [u8];
}

pub trait DecompressorFactory {
    fn create_decompressor(&self, w: Box<dyn Read>) -> io::Result<Box<dyn Decompressor>>;
    fn eof_marker(&self) -> &'static [u8];
    fn magic(&self) -> &'static [u8];
}

pub fn guess_decompressor<R: ReadSeek>(
    mut r: R,
) -> Result<Box<dyn DecompressorFactory>> {
    let len = r.seek(io::SeekFrom::End(0))?;

    let mut buf = [0u8; 128];
    let mut slice = &mut buf[0..(min(len, 128) as usize)];

    if len < 128 {
        r.seek(io::SeekFrom::Start(0))?;
    } else {
        r.seek(io::SeekFrom::Start(max(len - 128, 128)))?;
    }
    r.read_exact(&mut slice)?;

    let df = GzipDecompressorFactory::new();
    if slice.ends_with(df.eof_marker()) {
        return Ok(Box::new(df));
    }

    let df = XzDecompressorFactory::new();
    if slice.ends_with(df.eof_marker()) {
        return Ok(Box::new(df));
    }

    let df = ZstdDecompressorFactory::new();
    if slice.ends_with(df.eof_marker()) {
        return Ok(Box::new(df));
    }

    let df = PlainDecompressorFactory::new();
    if slice.ends_with(df.eof_marker()) {
        return Ok(Box::new(df));
    }

    Err(anyhow!("Found no known end marker"))
}
