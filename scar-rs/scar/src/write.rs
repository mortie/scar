use crate::compression::{Compressor, CompressorFactory};
use crate::pax;
use crate::util::{log10_ceil, Checkpoint};
use std::io::{self, Read, Write};
use std::rc::Rc;
use std::sync::atomic::{AtomicU64, Ordering};

pub struct TrackedWrite<W: Write> {
    pub w: W,
    written: Rc<AtomicU64>,
}

impl<W: Write> TrackedWrite<W> {
    pub fn new(w: W, written: Rc<AtomicU64>) -> Self {
        Self { w, written }
    }

    pub fn take(self) -> W {
        self.w
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
    pub typeflag: u8,
    pub data: Vec<u8>,
}

pub struct ScarWriter {
    w: Option<TrackedWrite<Box<dyn Compressor>>>,
    compressor_factory: Box<dyn CompressorFactory>,
    compressed_loc: Rc<AtomicU64>,
    raw_loc: Rc<AtomicU64>,

    checkpoints: Vec<Checkpoint>,
    scar_index: Vec<IndexEntry>,
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
            w: Some(writer),
            compressor_factory: cf,
            compressed_loc,
            raw_loc,

            checkpoints: Vec::new(),
            scar_index: Vec::new(),
            checkpoint_interval: 1024 * 1024,
            last_checkpoint_compressed_loc: 0,
        }
    }

    pub fn add_file<R: Read>(&mut self, r: &mut R, meta: &pax::Metadata) -> io::Result<()> {
        self.add_entry(meta)?;
        pax::write_content(self.w.as_mut().unwrap(), r, meta.size)?;

        Ok(())
    }

    pub fn add_entry(&mut self, meta: &pax::Metadata) -> io::Result<()> {
        self.consider_checkpoint()?;

        self.scar_index.push(IndexEntry {
            raw_loc: self.raw_loc.load(Ordering::Relaxed),
            typeflag: meta.typeflag.char(),
            data: meta.path.clone(),
        });

        pax::write_header(self.w.as_mut().unwrap(), meta)?;

        Ok(())
    }

    pub fn add_global_meta(&mut self, meta: &pax::PaxMeta) -> io::Result<()> {
        self.consider_checkpoint()?;

        let loc = self.raw_loc.load(Ordering::Relaxed);
        let data = meta.stringify();
        pax::write_raw_entry(
            self.w.as_mut().unwrap(),
            pax::MetaType::PaxGlobal.char(),
            &data,
        )?;

        self.scar_index.push(IndexEntry {
            raw_loc: loc,
            typeflag: b'g',
            data,
        });

        Ok(())
    }

    pub fn finish(mut self) -> io::Result<Box<dyn Write>> {
        let block = pax::new_block();
        self.w.as_mut().unwrap().write_all(&block)?;
        self.w.as_mut().unwrap().write_all(&block)?;

        self.checkpoint()?;
        let index_checkpoint = self.checkpoints.last().unwrap().clone();
        self.w.as_mut().unwrap().write_all(b"SCAR-INDEX\n")?;
        for entry in &self.scar_index {
            Self::write_entry(self.w.as_mut().unwrap(), entry)?;
        }

        self.checkpoint()?;
        let checkpoints_checkpoint = self.checkpoints.last().unwrap().clone();
        self.w.as_mut().unwrap().write_all(b"SCAR-CHECKPOINTS\n")?;
        for entry in &self.checkpoints {
            write!(
                self.w.as_mut().unwrap(),
                "{} {}\n",
                entry.compressed_loc,
                entry.raw_loc
            )?;
        }

        self.checkpoint()?;
        write!(
            self.w.as_mut().unwrap(),
            "SCAR-TAIL\n{}\n{}\n",
            index_checkpoint.compressed_loc,
            checkpoints_checkpoint.compressed_loc
        )?;

        let mut w = self.w.take().unwrap().take().finish()?;
        w.write_all(self.compressor_factory.eof_marker())?;
        Ok(w)
    }

    pub fn write_block(&mut self, block: &pax::Block) -> io::Result<()> {
        self.w.as_mut().unwrap().write_all(block)
    }

    fn consider_checkpoint(&mut self) -> io::Result<()> {
        self.w.as_mut().unwrap().flush()?;
        let compressed_loc = self.compressed_loc.load(Ordering::Relaxed);
        if compressed_loc - self.last_checkpoint_compressed_loc > self.checkpoint_interval {
            self.checkpoint()?;
        }

        Ok(())
    }

    fn checkpoint(&mut self) -> io::Result<()> {
        let mut w = self.w.take().unwrap().take();
        w.flush()?;
        let w = w.finish()?;

        let compressed_loc = self.compressed_loc.load(Ordering::Relaxed);
        self.last_checkpoint_compressed_loc = compressed_loc;
        self.checkpoints.push(Checkpoint {
            compressed_loc,
            raw_loc: self.raw_loc.load(Ordering::Relaxed),
        });

        self.w = Some(TrackedWrite {
            w: self.compressor_factory.create_compressor(w),
            written: self.raw_loc.clone(),
        });

        Ok(())
    }

    fn write_entry(w: &mut TrackedWrite<Box<dyn Compressor>>, ent: &IndexEntry) -> io::Result<()> {
        let len: u64 = 3 + log10_ceil(ent.raw_loc) + 1 + ent.data.len() as u64 + 1;
        let mut num_digits = log10_ceil(len);
        if log10_ceil(len + num_digits) > num_digits {
            num_digits += 1;
        }

        if ent.typeflag == pax::MetaType::PaxGlobal.char() {
            write!(
                w,
                "{} {} {} ",
                num_digits + len,
                ent.typeflag as char,
                ent.raw_loc
            )?;
            w.write_all(ent.data.as_slice())
        } else {
            write!(
                w,
                "{} {} {} ",
                num_digits + len,
                ent.typeflag as char,
                ent.raw_loc
            )?;
            w.write_all(ent.data.as_slice())?;
            w.write_all(b"\n")
        }
    }
}
