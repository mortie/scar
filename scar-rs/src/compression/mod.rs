use std::io::{self, Read, Write};

pub mod gzip;
pub use gzip::GzipCompressorFactory;

pub trait Compressor: Write {
    fn finish(self: Box<Self>) -> io::Result<Box<dyn Write>>;
}

pub trait Decompressor: Read {
    fn finish(self: Box<Self>) -> io::Result<Box<dyn Read>>;
}

pub trait CompressorFactory {
    fn create_compressor(&self, w: Box<dyn Write>) -> Box<dyn Compressor>;
}

pub trait DecompressorFactory {
    fn create_decompressor(&self, w: Box<dyn Read>) -> Box<dyn Decompressor>;
}
