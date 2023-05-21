use crate::compression::{self, Decompressor, DecompressorFactory};
use crate::pax;
use crate::util::{find_last_occurrence, read_num_from_bufread, Checkpoint, ReadSeek};
use std::cell::RefCell;
use std::cmp::min;
use std::fmt;
use std::io::{self, BufRead, BufReader, Read, Seek};
use std::iter::Iterator;
use std::rc::Rc;
use anyhow::{Result, anyhow};

#[derive(Clone)]
pub struct RSCell {
    r: Rc<RefCell<Box<dyn ReadSeek>>>,
}

impl RSCell {
    pub fn new(r: Rc<RefCell<Box<dyn ReadSeek>>>) -> Self {
        Self { r }
    }
}

impl Read for RSCell {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        self.r.borrow_mut().read(buf)
    }
}

impl Seek for RSCell {
    fn seek(&mut self, pos: io::SeekFrom) -> io::Result<u64> {
        self.r.borrow_mut().seek(pos)
    }
}

pub struct ScarReader {
    r: RSCell,
    df: Box<dyn DecompressorFactory>,
    compressed_index_loc: u64,
    checkpoints: Vec<Checkpoint>,
}

impl ScarReader {
    pub fn new<R: ReadSeek + 'static>(mut r: R) -> Result<Self> {
        let df = compression::guess_decompressor(&mut r)?;
        Self::create(Rc::new(RefCell::new(Box::new(r))), df)
    }

    pub fn with_decompressor<R: ReadSeek + 'static>(
        r: R,
        df: Box<dyn DecompressorFactory>,
    ) -> Result<Self> {
        Self::create(Rc::new(RefCell::new(Box::new(r))), df)
    }

    fn create(
        rc: Rc<RefCell<Box<dyn ReadSeek>>>,
        df: Box<dyn DecompressorFactory>,
    ) -> Result<Self> {
        let mut r = RSCell::new(rc.clone());

        let file_len = r.seek(io::SeekFrom::End(0))?;
        let tail_block_len = min(file_len, 512);
        if file_len <= 512 {
            r.seek(io::SeekFrom::Start(0))?;
        } else {
            r.seek(io::SeekFrom::Start(512))?;
        }
        let mut compressed_tail_block = [0; 512];
        let mut end = r.read(&mut compressed_tail_block)?;

        let magic = df.magic();
        loop {
            let idx = match find_last_occurrence(&compressed_tail_block[0..end], &magic) {
                Some(idx) => idx,
                None => return Err(anyhow!("Found no tail marker")),
            };
            end = idx + magic.len() - 1;

            r.seek(io::SeekFrom::End(idx as i64 - tail_block_len as i64))?;
            let dc = df.create_decompressor(Box::new(RSCell::new(rc.clone())))?;
            let mut br = io::BufReader::new(dc);

            let mut line = Vec::<u8>::new();
            if let Err(_) = br.read_until(b'\n', &mut line) {
                // An error here probably means that we started decoding at an invalid location,
                // and we have to seek from further back to find the proper start of the
                // compressed data.
                continue;
            }

            if line != b"SCAR-TAIL\n" {
                // Same here, if we don't find "SCAR-TAIL\n" we probably just
                // started decoding some garbage
                continue;
            }

            line.clear();
            br.read_until(b'\n', &mut line)?;
            let compressed_index_loc =
                String::from_utf8_lossy(&line[..line.len() - 1]).parse::<u64>()?;

            line.clear();
            br.read_until(b'\n', &mut line)?;
            let compressed_checkpoints_loc =
                String::from_utf8_lossy(&line[..line.len() - 1]).parse::<u64>()?;

            let checkpoints = Self::read_checkpoints(rc, compressed_checkpoints_loc, &df)?;

            return Ok(Self {
                r,
                df,
                compressed_index_loc,
                checkpoints,
            });
        }
    }

    fn read_checkpoints(
        rc: Rc<RefCell<Box<dyn ReadSeek>>>,
        compressed_checkpoints_loc: u64,
        df: &Box<dyn DecompressorFactory>,
    ) -> Result<Vec<Checkpoint>> {
        rc.borrow_mut()
            .seek(io::SeekFrom::Start(compressed_checkpoints_loc))?;
        let dc = df.create_decompressor(Box::new(RSCell::new(rc.clone())))?;
        let mut br = io::BufReader::new(dc);
        let mut chs = [0u8; 1];
        let mut checkpoints = Vec::<Checkpoint>::new();

        let mut line = Vec::<u8>::new();
        br.read_until(b'\n', &mut line)?;
        if line != b"SCAR-CHECKPOINTS\n" {
            return Err(anyhow!("Invalid checkpoints header"));
        }

        loop {
            let buf = match br.fill_buf() {
                Ok(buf) => buf,
                Err(_) => break,
            };

            if buf.len() == 0 || buf.starts_with(b"SCAR-TAIL\n") {
                break;
            }

            let (compressed_loc, _) = read_num_from_bufread(&mut br)?;

            br.read_exact(&mut chs)?;
            if chs[0] != b' ' {
                return Err(anyhow!("Invalid chunk"));
            }

            let (raw_loc, _) = read_num_from_bufread(&mut br)?;

            br.read_exact(&mut chs)?;
            if chs[0] != b'\n' {
                return Err(anyhow!("Invalid chunk"));
            }

            checkpoints.push(Checkpoint {
                compressed_loc,
                raw_loc,
            });
        }

        Ok(checkpoints)
    }

    pub fn index(&mut self) -> Result<IndexIter> {
        self.r
            .seek(io::SeekFrom::Start(self.compressed_index_loc))?;

        let mut br = BufReader::new(self.df.create_decompressor(Box::new(self.r.clone()))?);

        let mut line = Vec::<u8>::new();
        br.read_until(b'\n', &mut line)?;
        if line.as_slice() != b"SCAR-INDEX\n" {
            return Err(anyhow!("Invalid index header"));
        }

        let seek_pos = self.r.seek(io::SeekFrom::Current(0))?;

        Ok(IndexIter {
            r: self.r.clone(),
            br,
            global_meta: pax::PaxMeta::new(),
            seek_pos,
        })
    }

    pub fn read_item(
        &mut self,
        item: &IndexItem,
    ) -> Result<pax::PaxReader<Box<dyn Decompressor>>> {
        let dc = self.seek_to_raw_loc(item.offset)?;
        let mut pr = pax::PaxReader::new(dc);
        pr.global_meta = item.global_meta.clone();
        Ok(pr)
    }

    fn seek_to_raw_loc(&mut self, raw_loc: u64) -> io::Result<Box<dyn Decompressor>> {
        let mut checkpoint = Checkpoint {
            compressed_loc: 0,
            raw_loc: 0,
        };

        for ch in &self.checkpoints {
            if ch.raw_loc <= raw_loc {
                checkpoint = ch.clone();
            } else {
                break;
            }
        }

        self.r
            .seek(io::SeekFrom::Start(checkpoint.compressed_loc))?;
        let mut dc = self.df.create_decompressor(Box::new(self.r.clone()))?;

        let mut diff = raw_loc - checkpoint.raw_loc;
        let mut buf = [0u8; 1024];

        while diff >= 1024 {
            dc.read_exact(&mut buf)?;
            diff -= 1024;
        }

        if diff > 0 {
            dc.read_exact(&mut buf[0..(diff as usize)])?;
        }

        Ok(dc)
    }
}

pub struct IndexItem {
    pub path: Vec<u8>,
    pub typeflag: pax::FileType,
    pub offset: u64,
    pub global_meta: pax::PaxMeta,
}

impl fmt::Display for IndexItem {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "{:?}: {}",
            self.typeflag,
            String::from_utf8_lossy(self.path.as_slice())
        )
    }
}

pub struct IndexIter {
    r: RSCell,
    br: BufReader<Box<dyn Decompressor>>,
    global_meta: pax::PaxMeta,
    seek_pos: u64,
}

impl Iterator for IndexIter {
    type Item = Result<IndexItem>;

    fn next(&mut self) -> Option<Result<IndexItem>> {
        if let Err(err) = self.r.seek(io::SeekFrom::Start(self.seek_pos)) {
            return Some(Err(err.into()));
        }

        let b = match self.br.fill_buf() {
            Err(_) => return None,
            Ok(b) => b,
        };

        if b.len() == 0 || b.starts_with(b"SCAR-CHECKPOINTS\n") {
            return None;
        }

        let mut chs = [0u8; 1];

        let (field_length, field_num_digits) = match read_num_from_bufread(&mut self.br) {
            Ok(res) => (res.0 as usize, res.1),
            Err(err) => return Some(Err(err.into())),
        };

        if field_length > 16 * 1024 {
            return Some(Err(anyhow!("Too large index entry")));
        }

        if let Err(err) = self.br.read_exact(&mut chs) {
            return Some(Err(err.into()));
        } else if chs[0] != b' ' {
            return Some(Err(anyhow!("Invalid index entry")));
        }

        let typeflag = if let Err(err) = self.br.read_exact(&mut chs) {
            return Some(Err(err.into()));
        } else {
            chs[0]
        };

        if let Err(err) = self.br.read_exact(&mut chs) {
            return Some(Err(err.into()));
        } else if chs[0] != b' ' {
            return Some(Err(anyhow!("Invalid index entry")));
        }

        let (offset, offset_num_digits) = match read_num_from_bufread(&mut self.br) {
            Ok(res) => res,
            Err(err) => return Some(Err(err.into())),
        };

        if let Err(err) = self.br.read_exact(&mut chs) {
            return Some(Err(err.into()));
        } else if chs[0] != b' ' {
            return Some(Err(anyhow!("Invalid index entry")));
        }

        if let Some(t) = pax::MetaType::from_char(typeflag) {
            if t == pax::MetaType::PaxGlobal {
                let non_content_length = field_num_digits + 3 + offset_num_digits + 1;
                if field_length < non_content_length {
                    return Some(Err(anyhow!("Field length too short")));
                }

                let content_length = field_length - non_content_length;
                let mut content = Vec::<u8>::new();
                content.resize(content_length, 0);
                if let Err(err) = self.br.read_exact(content.as_mut_slice()) {
                    return Some(Err(err.into()));
                }

                if let Err(err) = self.global_meta.parse(&mut content.as_slice()) {
                    return Some(Err(err.into()));
                }

                // TODO: loop instead of recursion?
                return self.next();
            }
        }

        let non_content_length = field_num_digits + 3 + offset_num_digits + 2;
        if field_length < non_content_length {
            return Some(Err(anyhow!("Field length too short")));
        }

        let content_length = field_length - non_content_length;
        let mut content = Vec::<u8>::new();
        content.resize(content_length, 0);
        if let Err(err) = self.br.read_exact(content.as_mut_slice()) {
            return Some(Err(err.into()));
        }

        if let Err(err) = self.br.read_exact(&mut chs) {
            return Some(Err(err.into()));
        } else if chs[0] != b'\n' {
            return Some(Err(anyhow!("Invalid index entry")));
        }

        self.seek_pos = match self.r.seek(io::SeekFrom::Current(0)) {
            Ok(pos) => pos,
            Err(err) => return Some(Err(err.into())),
        };

        Some(Ok(IndexItem {
            path: content,
            typeflag: pax::FileType::from_char(typeflag),
            offset,
            global_meta: self.global_meta.clone(),
        }))
    }
}
