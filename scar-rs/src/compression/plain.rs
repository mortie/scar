use super::{Compressor, CompressorFactory, Decompressor, DecompressorFactory};
use std::io::{self, Read, Write};

struct PlainCompressor {
    w: Box<dyn Write>,
}

impl Write for PlainCompressor {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.w.write(buf)
    }

    fn flush(&mut self) -> io::Result<()> {
        self.w.flush()
    }
}

impl Compressor for PlainCompressor {
    fn finish(self: Box<Self>) -> io::Result<Box<dyn Write>> {
        Ok(self.w)
    }
}

pub struct PlainCompressorFactory {}

impl PlainCompressorFactory {
    pub fn new() -> Self {
        Self {}
    }
}

impl CompressorFactory for PlainCompressorFactory {
    fn create_compressor(&self, w: Box<dyn Write>) -> Box<dyn Compressor> {
        Box::new(PlainCompressor { w })
    }
}

struct PlainDecompressor {
    r: Box<dyn Read>,
}

impl Read for PlainDecompressor {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        self.r.read(buf)
    }
}

impl Decompressor for PlainDecompressor {
    fn finish(self: Box<Self>) -> Box<dyn Read> {
        self.r
    }
}

pub struct PlainDecompressorFactory {}

impl PlainDecompressorFactory {
    pub fn new() -> Self {
        Self {}
    }
}

impl DecompressorFactory for PlainDecompressorFactory {
    fn create_decompressor(&self, r: Box<dyn Read>) -> Box<dyn Decompressor> {
        Box::new(PlainDecompressor { r })
    }

    fn magic(&self) -> &'static [u8] {
        b"SCAR-TAIL\n"
    }
}
