use crate::compression::{Compressor, CompressorFactory};
use crate::pax;
use std::cell::RefCell;
use std::io::{self, Read, Write};
use std::rc::Rc;

#[derive(Debug)]
pub struct ContinuePoint {
    pub compressed_loc: u64,
    pub raw_loc: u64,
}

struct TrackedWrite {
    w: Box<dyn Write>,
    written: Rc<RefCell<u64>>,
}

impl Write for TrackedWrite {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        let num = self.w.write(buf)?;
        *self.written.borrow_mut() += num as u64;
        Ok(num)
    }

    fn flush(&mut self) -> io::Result<()> {
        self.w.flush()
    }
}

struct IndexEntry {
    raw_loc: u64,
    typeflag: pax::FileType,
    path: Vec<u8>,
}

pub struct ScarWriter {
    compressor_factory: Box<dyn CompressorFactory>,
    compressor: Box<dyn Compressor>,
    compressed_loc: Rc<RefCell<u64>>,
    raw_loc: u64,

    continue_points: Vec<ContinuePoint>,
    scar_index: Vec<IndexEntry>,
    bytes_since_checkpoint: u64,
    checkpoint_interval: u64,
    last_checkpoint_compressed_loc: u64,
}

impl ScarWriter {
    pub fn new(cf: Box<dyn CompressorFactory>, w: Box<dyn Write>) -> Self {
        let compressed_loc = Rc::new(RefCell::new(0u64));
        let tracked_write = Box::new(TrackedWrite {
            w,
            written: compressed_loc.clone(),
        });
        let compressor = cf.create_compressor(tracked_write);

        Self {
            compressor_factory: cf,
            compressor,
            compressed_loc,
            raw_loc: 0u64,

            continue_points: Vec::new(),
            scar_index: Vec::new(),
            bytes_since_checkpoint: 0,
            checkpoint_interval: 1024 * 1024,
            last_checkpoint_compressed_loc: 0,
        }
    }

    pub fn add_file<R: Read>(&mut self, meta: &pax::Metadata, r: R) -> io::Result<()> {
        self.consider_checkpoint()?;

        self.scar_index.push(IndexEntry {
            raw_loc: self.raw_loc,
            typeflag: meta.typeflag,
            path: meta.path.clone(),
        });

        pax::write_header(&mut self.compressor, meta)?;

        Ok(())
    }

    fn consider_checkpoint(&mut self) -> io::Result<()> {
        self.compressor.flush()?;
        let mut compressed_loc = *self.compressed_loc.borrow();
        if compressed_loc - self.last_checkpoint_compressed_loc > self.checkpoint_interval {
            let w = self.compressor.finish()?;

            compressed_loc = *self.compressed_loc.borrow();
            self.last_checkpoint_compressed_loc = compressed_loc;
            self.continue_points.push(ContinuePoint {
                compressed_loc,
                raw_loc: self.raw_loc,
            });

            self.compressor = self.compressor_factory.create_compressor(w);
        }

        Ok(())
    }
}
