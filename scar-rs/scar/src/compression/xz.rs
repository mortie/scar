use super::{Compressor, CompressorFactory, Decompressor, DecompressorFactory};
use std::io::{self, Read, Write};
use xz2;

const EOF_MARKER: &[u8] = &[
    0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00, 0x00, 0x04, 0xe6, 0xd6, 0xb4, 0x46, 0x02, 0x00, 0x21, 0x01,
    0x1c, 0x00, 0x00, 0x00, 0x10, 0xcf, 0x58, 0xcc, 0x01, 0x00, 0x08, 0x53, 0x43, 0x41, 0x52, 0x2d,
    0x45, 0x4f, 0x46, 0x0a, 0x00, 0x00, 0x00, 0x00, 0xa2, 0x8d, 0xf2, 0xf6, 0x3c, 0xcc, 0x0f, 0xcb,
    0x00, 0x01, 0x21, 0x09, 0x6c, 0x18, 0xc5, 0xd5, 0x1f, 0xb6, 0xf3, 0x7d, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x04, 0x59, 0x5a,
];

const MAGIC: &[u8] = &[0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00];

struct XzCompressor {
    w: xz2::write::XzEncoder<Box<dyn Write>>,
}

impl Write for XzCompressor {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.w.write(buf)
    }

    fn flush(&mut self) -> io::Result<()> {
        self.w.flush()
    }
}

impl Compressor for XzCompressor {
    fn finish(mut self: Box<Self>) -> io::Result<Box<dyn Write>> {
        self.w.try_finish()?;
        self.w.finish()
    }
}

pub struct XzCompressorFactory {
    level: u32,
}

impl XzCompressorFactory {
    pub fn new(level: u32) -> Self {
        Self { level }
    }
}

impl CompressorFactory for XzCompressorFactory {
    fn create_compressor(&self, w: Box<dyn Write>) -> io::Result<Box<dyn Compressor>> {
        Ok(Box::new(XzCompressor {
            w: xz2::write::XzEncoder::new(w, self.level),
        }))
    }

    fn eof_marker(&self) -> &'static [u8] {
        EOF_MARKER
    }
}

struct XzDecompressor {
    r: xz2::read::XzDecoder<Box<dyn Read>>,
}

impl Read for XzDecompressor {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        self.r.read(buf)
    }
}

impl Decompressor for XzDecompressor {
    fn finish(self: Box<Self>) -> Box<dyn Read> {
        self.r.into_inner()
    }
}

pub struct XzDecompressorFactory {}

impl XzDecompressorFactory {
    pub fn new() -> Self {
        Self {}
    }
}

impl DecompressorFactory for XzDecompressorFactory {
    fn create_decompressor(&self, r: Box<dyn Read>) -> io::Result<Box<dyn Decompressor>> {
        Ok(Box::new(XzDecompressor {
            r: xz2::read::XzDecoder::new(r),
        }))
    }

    fn eof_marker(&self) -> &'static [u8] {
        EOF_MARKER
    }

    fn magic(&self) -> &'static [u8] {
        MAGIC
    }
}
