use crate::compression::{self, Decompressor, DecompressorFactory};
use crate::pax;
use crate::util::{find_last_occurrence, read_num_from_bufread, ContinuePoint};
use std::cell::RefCell;
use std::cmp::min;
use std::error::Error;
use std::fmt;
use std::io::{self, BufRead, BufReader, Read, Seek};
use std::iter::{IntoIterator, Iterator};
use std::rc::Rc;

pub trait ReadSeek: Read + Seek {}
impl<T> ReadSeek for T where T: Read + Seek {}

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
    chunks: Vec<ContinuePoint>,
}

impl ScarReader {
    pub fn new(r: Box<dyn ReadSeek>) -> Result<Self, Box<dyn Error>> {
        Self::try_new(Rc::new(RefCell::new(r)))
    }

    pub fn try_new(rc: Rc<RefCell<Box<dyn ReadSeek>>>) -> Result<Self, Box<dyn Error>> {
        let df = Box::new(compression::GzipDecompressorFactory::new());
        match Self::try_decompressor(rc.clone(), df) {
            Ok(res) => return Ok(res),
            Err(err) => println!("oh no {}", err),
        };

        let df = Box::new(compression::PlainDecompressorFactory::new());
        Self::try_decompressor(rc, df)
    }

    pub fn try_decompressor(
        rc: Rc<RefCell<Box<dyn ReadSeek>>>,
        df: Box<dyn DecompressorFactory>,
    ) -> Result<Self, Box<dyn Error>> {
        let mut r = RSCell::new(rc.clone());

        r.seek(io::SeekFrom::End(-512))?;
        let mut compressed_tail_block = [0; 512];
        let mut end = r.read(&mut compressed_tail_block)?;

        let magic = df.magic();
        loop {
            let idx = match find_last_occurrence(&compressed_tail_block[0..end], &magic) {
                Some(idx) => idx,
                None => return Err("Found no tail marker".into()),
            };
            end = idx + magic.len() - 1;

            r.seek(io::SeekFrom::End(-512 + idx as i64))?;
            let dc = df.create_decompressor(Box::new(RSCell::new(rc.clone())));
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
            let compressed_chunks_loc =
                String::from_utf8_lossy(&line[..line.len() - 1]).parse::<u64>()?;

            let chunks = Self::read_chunks(rc, compressed_chunks_loc, &df)?;

            return Ok(Self {
                r,
                df,
                compressed_index_loc,
                chunks,
            });
        }
    }

    fn read_chunks(
        rc: Rc<RefCell<Box<dyn ReadSeek>>>,
        compressed_chunks_loc: u64,
        df: &Box<dyn DecompressorFactory>,
    ) -> Result<Vec<ContinuePoint>, Box<dyn Error>> {
        rc.borrow_mut().seek(io::SeekFrom::Start(compressed_chunks_loc))?;
        let dc = df.create_decompressor(Box::new(RSCell::new(rc.clone())));
        let mut br = io::BufReader::new(dc);
        let mut chs = [0u8; 1];
        let mut chunks = Vec::<ContinuePoint>::new();

        let mut line = Vec::<u8>::new();
        br.read_until(b'\n', &mut line)?;
        if line != b"SCAR-CHUNKS\n" {
            return Err("Invalid chunk header\n".into());
        }

        while br.fill_buf()?.len() > 0 {
            let (compressed_loc, _) = read_num_from_bufread(&mut br)?;

            br.read_exact(&mut chs)?;
            if chs[0] != b' ' {
                return Err("Invalid chunk".into());
            }

            let (raw_loc, _) = read_num_from_bufread(&mut br)?;

            br.read_exact(&mut chs)?;
            if chs[0] != b'\n' {
                return Err("Invalid chunk".into());
            }

            chunks.push(ContinuePoint {
                compressed_loc,
                raw_loc,
            });
        }

        Ok(chunks)
    }

    pub fn index(&mut self) -> Result<IndexIter, Box<dyn Error>> {
        self.r
            .seek(io::SeekFrom::Start(self.compressed_index_loc))?;

        let mut r = BufReader::new(self.df.create_decompressor(Box::new(self.r.clone())));

        let mut line = Vec::<u8>::new();
        r.read_until(b'\n', &mut line)?;
        if line.as_slice() != b"SCAR-INDEX\n" {
            return Err("Invalid index header".into());
        }

        Ok(IndexIter {
            r,
            global_meta: pax::PaxMeta::new(),
        })
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
    r: BufReader<Box<dyn Decompressor>>,
    global_meta: pax::PaxMeta,
}

impl Iterator for IndexIter {
    type Item = Result<IndexItem, Box<dyn Error>>;

    fn next(&mut self) -> Option<Result<IndexItem, Box<dyn Error>>> {
        let b = match self.r.fill_buf() {
            Err(err) => return Some(Err(err.into())),
            Ok(b) => b,
        };

        if b.len() == 0 {
            return None;
        }

        let mut chs = [0u8; 1];

        let (field_length, field_num_digits) = match read_num_from_bufread(&mut self.r) {
            Ok(res) => (res.0 as usize, res.1),
            Err(err) => return Some(Err(Box::new(err))),
        };

        if let Err(err) = self.r.read_exact(&mut chs) {
            return Some(Err(Box::new(err)));
        } else if chs[0] != b' ' {
            return Some(Err("Invalid index entry".into()));
        }

        let typeflag = if let Err(err) = self.r.read_exact(&mut chs) {
            return Some(Err(Box::new(err)));
        } else {
            chs[0]
        };

        if let Err(err) = self.r.read_exact(&mut chs) {
            return Some(Err(Box::new(err)));
        } else if chs[0] != b' ' {
            return Some(Err("Invalid index entry".into()));
        }

        let (offset, offset_num_digits) = match read_num_from_bufread(&mut self.r) {
            Ok(res) => res,
            Err(err) => return Some(Err(Box::new(err))),
        };

        if let Err(err) = self.r.read_exact(&mut chs) {
            return Some(Err(Box::new(err)));
        } else if chs[0] != b' ' {
            return Some(Err("Invalid index entry".into()));
        }

        if let Some(t) = pax::MetaType::from_char(typeflag) {
            if t == pax::MetaType::PaxGlobal {
                let content_length = field_length - field_num_digits - 3 - offset_num_digits - 1;
                let mut content = Vec::<u8>::new();
                content.resize(content_length, 0);
                if let Err(err) = self.r.read_exact(content.as_mut_slice()) {
                    return Some(Err(Box::new(err)));
                }

                if let Err(err) = self.global_meta.parse(&mut content.as_slice()) {
                    return Some(Err(err));
                }

                // TODO: loop instead of recursion?
                return self.next();
            }
        }

        let content_length = field_length - field_num_digits - 3 - offset_num_digits - 2;
        let mut content = Vec::<u8>::new();
        content.resize(content_length, 0);
        if let Err(err) = self.r.read_exact(content.as_mut_slice()) {
            return Some(Err(Box::new(err)));
        }

        if let Err(err) = self.r.read_exact(&mut chs) {
            return Some(Err(Box::new(err)));
        } else if chs[0] != b'\n' {
            return Some(Err("Invalid index entry".into()));
        }

        Some(Ok(IndexItem {
            path: content,
            typeflag: pax::FileType::from_char(typeflag),
            offset,
            global_meta: self.global_meta.clone(),
        }))
    }
}
