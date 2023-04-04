use crate::compression::{Compressor, CompressorFactory};
use crate::pax;
use crate::util;
use std::sync::atomic::{AtomicU64, Ordering};
use std::io::{self, Read, Write};
use std::rc::Rc;

#[derive(Debug, Clone)]
pub struct ContinuePoint {
    pub compressed_loc: u64,
    pub raw_loc: u64,
}

pub struct TrackedWrite<W: Write> {
    pub w: W,
    written: Rc<AtomicU64>,
}

impl<W: Write> TrackedWrite<W> {
    pub fn new(w: W, written: Rc<AtomicU64>) -> Self {
        Self { w, written }
    }
}

impl<W: Write> Write for TrackedWrite<W> {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        let num = self.w.write(buf)?;
        self.written.fetch_add(num as u64, Ordering::Relaxed);
        Ok(num)
    }

    fn flush(&mut self) -> io::Result<()> {
        self.w.flush()
    }
}

struct IndexEntry {
    pub raw_loc: u64,
    pub typeflag: pax::FileType,
    pub data: Vec<u8>,
}

pub struct ScarWriter {
    pub w: TrackedWrite<Box<dyn Compressor>>,
    compressor_factory: Box<dyn CompressorFactory>,
    compressed_loc: Rc<AtomicU64>,
    raw_loc: Rc<AtomicU64>,

    continue_points: Vec<ContinuePoint>,
    scar_index: Vec<IndexEntry>,
    bytes_since_checkpoint: u64,
    checkpoint_interval: u64,
    last_checkpoint_compressed_loc: u64,
}

impl ScarWriter {
    pub fn new(cf: Box<dyn CompressorFactory>, w: Box<dyn Write>) -> Self {
        let compressed_loc = Rc::new(AtomicU64::new(0));
        let raw_loc = Rc::new(AtomicU64::new(0));
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
            raw_loc: self.raw_loc.load(Ordering::Relaxed),
            typeflag: meta.typeflag,
            data: meta.path.clone(),
        });

        pax::write_header(&mut self.w, meta)?;
        Ok(())
    }

    pub fn finish(mut self) -> io::Result<Box<dyn Write>> {
        let block = pax::new_block();
        self.w.write_all(&block)?;
        self.w.write_all(&block)?;

        self.checkpoint()?;
        let index_checkpoint = self.continue_points.last().unwrap().clone();
        self.w.write_all(b"SCAR-INDEX\n")?;
        for entry in &self.scar_index {
            Self::write_entry(&mut self.w, entry)?;
        }

        self.checkpoint()?;
        let chunks_checkpoint = self.continue_points.last().unwrap().clone();
        self.w.write_all(b"SCAR-CHUNKS\n")?;
        for entry in &self.continue_points {
            write!(self.w, "{} {}\n", entry.compressed_loc, entry.raw_loc)?;
        }

        self.checkpoint()?;
        write!(
            self.w,
            "SCAR-TAIL\n{}\n{}\n",
            index_checkpoint.compressed_loc, chunks_checkpoint.compressed_loc
        )?;

        self.w.w.finish()
    }

    fn consider_checkpoint(&mut self) -> io::Result<()> {
        self.w.flush()?;
        let compressed_loc = self.compressed_loc.load(Ordering::Relaxed);
        if compressed_loc - self.last_checkpoint_compressed_loc > self.checkpoint_interval {
            self.checkpoint()?;
        }

        Ok(())
    }

    fn checkpoint(&mut self) -> io::Result<()> {
        let w = self.w.w.finish()?;

        let compressed_loc = self.compressed_loc.load(Ordering::Relaxed);
        self.last_checkpoint_compressed_loc = compressed_loc;
        self.continue_points.push(ContinuePoint {
            compressed_loc,
            raw_loc: self.raw_loc.load(Ordering::Relaxed),
        });

        self.w.w = self.compressor_factory.create_compressor(w);
        Ok(())
    }

    fn write_entry(w: &mut TrackedWrite<Box<dyn Compressor>>, ent: &IndexEntry) -> io::Result<()> {
        let len: u64 = 3 + util::log10_ceil(ent.raw_loc) + 1 + ent.data.len() as u64 + 1;
        let mut num_digits = util::log10_ceil(len);
        if util::log10_ceil(len + num_digits) > num_digits {
            num_digits += 1;
        }

        write!(w, "{} {} {} ", num_digits + len, ent.typeflag.char() as char, ent.raw_loc)?;
        w.write_all(ent.data.as_slice())?;
        w.write_all(b"\n")
    }
}
