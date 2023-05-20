use super::{Compressor, CompressorFactory, Decompressor, DecompressorFactory};
use std::io::{self, BufReader, Read, Write};
use zstd;

const EOF_MARKER: &[u8] = &[
    0x28, 0xb5, 0x2f, 0xfd, 0x04, 0x58, 0x49, 0x00, 0x00, 0x53, 0x43, 0x41, 0x52, 0x2d, 0x45, 0x4f,
    0x46, 0x0a, 0x3a, 0xb2, 0x49, 0x61,
];

const MAGIC: &[u8] = &[0x28, 0xb5, 0x2f, 0xfd];

struct ZstdCompressor<'a> {
    w: zstd::Encoder<'a, Box<dyn Write>>,
}

impl<'a> Write for ZstdCompressor<'a> {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.w.write(buf)
    }

    fn flush(&mut self) -> io::Result<()> {
        self.w.flush()
    }
}

impl<'a> Compressor for ZstdCompressor<'a> {
    fn finish(self: Box<Self>) -> io::Result<Box<dyn Write>> {
        self.w.finish()
    }
}

pub struct ZstdCompressorFactory {
    level: u32,
}

impl ZstdCompressorFactory {
    pub fn new(level: u32) -> Self {
        Self { level }
    }
}

impl CompressorFactory for ZstdCompressorFactory {
    fn create_compressor(&self, w: Box<dyn Write>) -> io::Result<Box<dyn Compressor>> {
        Ok(Box::new(ZstdCompressor {
            w: zstd::Encoder::new(w, self.level as i32)?,
        }))
    }

    fn eof_marker(&self) -> &'static [u8] {
        EOF_MARKER
    }
}

struct ZstdDecompressor<'a> {
    r: zstd::Decoder<'a, BufReader<Box<dyn Read>>>,
}

impl<'a> Read for ZstdDecompressor<'a> {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        self.r.read(buf)
    }
}

impl<'a> Decompressor for ZstdDecompressor<'a> {
    fn finish(self: Box<Self>) -> Box<dyn Read> {
        self.r.finish().into_inner()
    }
}

pub struct ZstdDecompressorFactory {}

impl ZstdDecompressorFactory {
    pub fn new() -> Self {
        Self {}
    }
}

impl DecompressorFactory for ZstdDecompressorFactory {
    fn create_decompressor(&self, r: Box<dyn Read>) -> io::Result<Box<dyn Decompressor>> {
        match zstd::Decoder::new(r) {
            Err(err) => Err(err),
            Ok(r) => Ok(Box::new(ZstdDecompressor { r })),
        }
    }

    fn eof_marker(&self) -> &'static [u8] {
        EOF_MARKER
    }

    fn magic(&self) -> &'static [u8] {
        MAGIC
    }
}
