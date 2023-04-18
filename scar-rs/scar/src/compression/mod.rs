use crate::util::ReadSeek;
use std::cmp::{max, min};
use std::error::Error;
use std::io::{self, Read, Write};

pub mod gzip;
pub use gzip::GzipCompressorFactory;
pub use gzip::GzipDecompressorFactory;

pub mod plain;
pub use plain::PlainCompressorFactory;
pub use plain::PlainDecompressorFactory;

pub trait Compressor: Write {
    fn finish(self: Box<Self>) -> io::Result<Box<dyn Write>>;
}

pub trait Decompressor: Read {
    fn finish(self: Box<Self>) -> Box<dyn Read>;
}

pub trait CompressorFactory {
    fn create_compressor(&self, w: Box<dyn Write>) -> Box<dyn Compressor>;
    fn eof_marker(&self) -> &'static [u8];
}

pub trait DecompressorFactory {
    fn create_decompressor(&self, w: Box<dyn Read>) -> Box<dyn Decompressor>;
    fn eof_marker(&self) -> &'static [u8];
    fn magic(&self) -> &'static [u8];
}

pub fn guess_decompressor<R: ReadSeek>(
    mut r: R,
) -> Result<Box<dyn DecompressorFactory>, Box<dyn Error>> {
    let len = r.seek(io::SeekFrom::End(0))?;

    let mut buf = [0u8; 32];
    let mut slice = &mut buf[0..(min(len, 32) as usize)];

    r.seek(io::SeekFrom::Start(max(len - 32, 32)))?;
    r.read_exact(&mut slice)?;

    let df = GzipDecompressorFactory::new();
    if slice.ends_with(df.eof_marker()) {
        return Ok(Box::new(df));
    }

    let df = PlainDecompressorFactory::new();
    if slice.ends_with(df.eof_marker()) {
        return Ok(Box::new(df));
    }

    Err("Found no known end marker".into())
}
