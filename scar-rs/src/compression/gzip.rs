use super::{Compressor, CompressorFactory, Decompressor, DecompressorFactory};
use flate2;
use std::error::Error;
use std::io::{self, Read, Write};

struct GzipCompressor {
    w: flate2::write::GzEncoder<Box<dyn Write>>,
}

impl Write for GzipCompressor {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.w.write(buf)
    }

    fn flush(&mut self) -> io::Result<()> {
        self.w.flush()
    }
}

impl Compressor for GzipCompressor {
    fn finish(mut self: Box<Self>) -> io::Result<Box<dyn Write>> {
        self.w.try_finish()?;
        self.w.finish()
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
            w: flate2::write::GzEncoder::new(w, self.level),
        })
    }
}

struct GzipDecompressor {
    r: flate2::read::GzDecoder<Box<dyn Read>>,
}

impl Read for GzipDecompressor {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        self.r.read(buf)
    }
}

impl Decompressor for GzipDecompressor {
    fn finish(self: Box<Self>) -> Box<dyn Read> {
        self.r.into_inner()
    }
}

pub struct GzipDecompressorFactory {}

impl GzipDecompressorFactory {
    pub fn new() -> Self {
        Self {}
    }
}

impl DecompressorFactory for GzipDecompressorFactory {
    fn create_decompressor(&self, r: Box<dyn Read>) -> Box<dyn Decompressor> {
        Box::new(GzipDecompressor {
            r: flate2::read::GzDecoder::new(r),
        })
    }

    fn magic(&self) -> &'static [u8] {
        b"\x1f\x8b"
    }
}
