use super::{Compressor, CompressorFactory, Decompressor, DecompressorFactory};
use flate2;
use std::error::Error;
use std::io::{self, Write};

struct GzipCompressor {
    w: Option<flate2::write::GzEncoder<Box<dyn Write>>>,
}

impl Write for GzipCompressor {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.w.as_mut().unwrap().write(buf)
    }

    fn flush(&mut self) -> io::Result<()> {
        self.w.as_mut().unwrap().flush()
    }
}

impl Compressor for GzipCompressor {
    fn finish(&mut self) -> io::Result<Box<dyn Write>> {
        self.w.take().unwrap().finish()
    }
}

pub struct GzipCompressorFactory {
    level: flate2::Compression,
}

impl GzipCompressorFactory {
    pub fn new(level: u32) -> Self {
        Self {
            level: flate2::Compression::new(level),
        }
    }
}

impl CompressorFactory for GzipCompressorFactory {
    fn create_compressor(&self, w: Box<dyn Write>) -> Box<dyn Compressor> {
        Box::new(GzipCompressor {
            w: Some(flate2::write::GzEncoder::new(w, self.level)),
        })
    }
}
