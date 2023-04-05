use crate::compression::DecompressorFactory;
use std::io::{self, Read, Seek};

pub trait ReadSeek: Read + Seek {}
impl<T> ReadSeek for T where T: Read + Seek {}

pub struct ScarReader {
    pub r: Box<dyn ReadSeek>,
}

impl ScarReader {
    pub fn new(df: Box<dyn DecompressorFactory>, r: Box<dyn ReadSeek>) -> Self {
        Self { r }
    }
}
