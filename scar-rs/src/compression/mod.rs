use std::io::{self, Write};

pub mod gzip;
pub use gzip::GzipCompressorFactory;

pub trait Compressor: Write {
    fn finish(&mut self) -> io::Result<Box<dyn Write>>;
}

pub trait Decompressor: Write {
    fn finish(&mut self) -> io::Result<Box<dyn Write>>;
}

pub trait CompressorFactory {
    fn create_compressor(&self, w: Box<dyn Write>) -> Box<dyn Compressor>;
}

pub trait DecompressorFactory<W: Write> {
    fn create_decompressor(&self, w: Box<dyn Write>) -> Box<dyn Decompressor>;
}
