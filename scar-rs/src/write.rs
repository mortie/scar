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

pub struct TrackedWrite<W: Write> {
    pub w: W,
    written: Rc<RefCell<u64>>,
}

impl<W: Write> TrackedWrite<W> {
    pub fn new(w: W, written: Rc<RefCell<u64>>) -> Self {
        Self { w, written }
    }
}

impl<W: Write> Write for TrackedWrite<W> {
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
    pub w: TrackedWrite<Box<dyn Compressor>>,
    compressor_factory: Box<dyn CompressorFactory>,
    compressed_loc: Rc<RefCell<u64>>,
    raw_loc: Rc<RefCell<u64>>,

    continue_points: Vec<ContinuePoint>,
    scar_index: Vec<IndexEntry>,
    bytes_since_checkpoint: u64,
    checkpoint_interval: u64,
    last_checkpoint_compressed_loc: u64,
}

impl ScarWriter {
    pub fn new(cf: Box<dyn CompressorFactory>, w: Box<dyn Write>) -> Self {
        let compressed_loc = Rc::new(RefCell::new(0u64));
        let raw_loc = Rc::new(RefCell::new(0u64));
        let compressor =
            cf.create_compressor(Box::new(TrackedWrite::new(w, compressed_loc.clone())));
        let writer = TrackedWrite::new(compressor, raw_loc.clone());

        Self {
            w: writer,
            compressor_factory: cf,
            compressed_loc,
            raw_loc,

            continue_points: Vec::new(),
            scar_index: Vec::new(),
            bytes_since_checkpoint: 0,
            checkpoint_interval: 1024 * 1024,
            last_checkpoint_compressed_loc: 0,
        }
    }

    pub fn add_file<R: Read>(&mut self, r: &mut R, meta: &pax::Metadata) -> io::Result<()> {
        self.add_entry(meta)?;
        pax::write_content(&mut self.w, r, meta.size)?;
        Ok(())
    }

    pub fn add_entry(&mut self, meta: &pax::Metadata) -> io::Result<()> {
        self.consider_checkpoint()?;

        self.scar_index.push(IndexEntry {
            raw_loc: *self.raw_loc.borrow(),
            typeflag: meta.typeflag,
            path: meta.path.clone(),
        });

        pax::write_header(&mut self.w, meta)?;
        Ok(())
    }

    pub fn finish(mut self) -> io::Result<Box<dyn Write>> {
        let block = pax::new_block();
        self.w.write_all(&block)?;
        self.w.write_all(&block)?;

        // TODO: Write the index and stuff

        self.w.w.finish()
    }

    fn consider_checkpoint(&mut self) -> io::Result<()> {
        self.w.flush()?;
        let mut compressed_loc = *self.compressed_loc.borrow();
        if compressed_loc - self.last_checkpoint_compressed_loc > self.checkpoint_interval {
            let w = self.w.w.finish()?;

            compressed_loc = *self.compressed_loc.borrow();
            self.last_checkpoint_compressed_loc = compressed_loc;
            self.continue_points.push(ContinuePoint {
                compressed_loc,
                raw_loc: *self.raw_loc.borrow(),
            });

            self.w.w = self.compressor_factory.create_compressor(w);
        }

        Ok(())
    }
}
