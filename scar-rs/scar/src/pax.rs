use crate::util;
use std::cmp::min;
use std::fmt;
use std::io::{Read, Write};
use std::mem::size_of;
use anyhow::{Result, anyhow};

#[derive(Copy, Clone)]
pub struct UStarHdrField {
    pub start: usize,
    pub length: usize,
}

macro_rules! ustar_field {
    ($name: ident, $start: literal, $length: literal) => {
        pub const $name: UStarHdrField = UStarHdrField {
            start: $start,
            length: $length,
        };
    };
}

ustar_field!(UST_NAME, 0, 100);
ustar_field!(UST_MODE, 100, 8);
ustar_field!(UST_UID, 108, 8);
ustar_field!(UST_GID, 116, 8);
ustar_field!(UST_SIZE, 124, 12);
ustar_field!(UST_MTIME, 136, 12);
ustar_field!(UST_CHKSUM, 148, 8);
ustar_field!(UST_TYPEFLAG, 156, 1);
ustar_field!(UST_LINKNAME, 157, 100);
ustar_field!(UST_MAGIC, 257, 6);
ustar_field!(UST_VERSION, 263, 2);
ustar_field!(UST_UNAME, 265, 32);
ustar_field!(UST_GNAME, 297, 32);
ustar_field!(UST_DEVMAJOR, 329, 8);
ustar_field!(UST_DEVMINOR, 337, 8);
ustar_field!(UST_PREFIX, 345, 155);

pub type Block = [u8; 512];

pub fn new_block() -> Block {
    [0u8; size_of::<Block>()]
}

pub fn block_write_str(block: &mut Block, field: UStarHdrField, data: &[u8]) {
    let length = min(field.length, data.len());
    block[field.start..(field.start + length)].copy_from_slice(&data[0..length])
}

pub fn block_write_octal(block: &mut Block, field: UStarHdrField, num: u64) {
    write!(
        &mut block[field.start..(field.start + field.length)],
        "{:0>1$o}\0",
        num % 8u64.pow((field.length - 1) as u32),
        field.length - 1
    )
    .unwrap();
}

pub fn block_read_octal(block: &Block, field: UStarHdrField) -> u64 {
    let mut num = 0u64;
    for i in field.start..(field.start + field.length) {
        let ch = block[i];

        if ch == b' ' || ch == b'\0' {
            break;
        }

        num = num.wrapping_mul(8);
        if ch >= b'0' && ch < b'8' {
            num = num.wrapping_add((ch - b'0') as u64);
        }
    }

    num
}

pub fn block_read_str(block: &Block, field: UStarHdrField) -> Vec<u8> {
    let mut len = 0usize;
    for i in field.start..(field.start + field.length) {
        if block[i] == b'\0' {
            break;
        }

        len += 1;
    }

    Vec::from(&block[field.start..(field.start + len)])
}

pub fn block_read_path(block: &Block, field: UStarHdrField) -> Vec<u8> {
    let mut s = block_read_str(block, UST_PREFIX);
    if s.len() == 0 {
        return block_read_str(block, field);
    }

    s.push(b'/');
    s.append(&mut block_read_str(block, field));
    s
}

pub fn block_read_size(block: &Block, field: UStarHdrField) -> u64 {
    if block[field.start] < 128 {
        return block_read_octal(block, field);
    }

    let mut num = (block[field.start] & 0x7f) as u64;
    for i in (field.start + 1)..(field.start + field.length) {
        num = num.wrapping_mul(256);
        num = num.wrapping_add(block[i] as u64);
    }

    num
}

#[derive(Copy, Clone, Debug, PartialEq)]
pub enum FileType {
    File,
    Hardlink,
    Symlink,
    Chardev,
    Blockdev,
    Directory,
    Fifo,
    Unknown,
}

impl FileType {
    pub fn char(self) -> u8 {
        match self {
            Self::File => b'0',
            Self::Hardlink => b'1',
            Self::Symlink => b'2',
            Self::Chardev => b'3',
            Self::Blockdev => b'4',
            Self::Directory => b'5',
            Self::Fifo => b'6',
            Self::Unknown => b'?',
        }
    }

    pub fn from_char(ch: u8) -> Self {
        match ch {
            b'0' => Self::File,
            b'\0' => Self::File,
            b'7' => Self::File,
            b'1' => Self::Hardlink,
            b'2' => Self::Symlink,
            b'3' => Self::Chardev,
            b'4' => Self::Blockdev,
            b'5' => Self::Directory,
            b'6' => Self::Fifo,
            _ => Self::Unknown,
        }
    }
}

#[derive(Copy, Clone, Debug, PartialEq)]
pub enum MetaType {
    PaxNext,
    PaxGlobal,
    GnuPath,
    GnuLinkPath,
}

impl MetaType {
    pub fn char(self) -> u8 {
        match self {
            Self::PaxNext => b'x',
            Self::PaxGlobal => b'g',
            Self::GnuPath => b'L',
            Self::GnuLinkPath => b'K',
        }
    }

    pub fn from_char(ch: u8) -> Option<Self> {
        match ch {
            b'x' => Some(Self::PaxNext),
            b'g' => Some(Self::PaxGlobal),
            b'L' => Some(Self::GnuPath),
            b'K' => Some(Self::GnuLinkPath),
            _ => None,
        }
    }
}

#[derive(Debug)]
pub struct Metadata {
    pub typeflag: FileType,
    pub mode: u32,
    pub devmajor: u32,
    pub devminor: u32,

    pub atime: Option<f64>,
    pub charset: Option<Vec<u8>>,
    pub comment: Option<Vec<u8>>,
    pub gid: u64,
    pub gname: Vec<u8>,
    pub hdrcharset: Option<Vec<u8>>,
    pub linkpath: Vec<u8>,
    pub mtime: f64,
    pub path: Vec<u8>,
    pub size: u64,
    pub uid: u64,
    pub uname: Vec<u8>,
}

impl fmt::Display for Metadata {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let fmt_option_str = |f: &mut fmt::Formatter, name: &str, s: &Option<Vec<u8>>| match s {
            Some(s) => write!(f, "\t{}: Some({})\n", name, &String::from_utf8_lossy(s)),
            None => write!(f, "\t{}: None\n", name),
        };

        write!(f, "Metadata{{\n")?;
        write!(f, "\ttypeflag: {:?}\n", self.typeflag)?;
        write!(f, "\tmode: 0{:o}\n", self.mode)?;
        write!(f, "\tdevmajor: {}\n", self.devmajor)?;
        write!(f, "\tdevminor: {}\n", self.devminor)?;
        write!(f, "\tatime: {:?}\n", self.atime)?;
        fmt_option_str(f, "charset", &self.charset)?;
        fmt_option_str(f, "comment", &self.comment)?;
        write!(f, "\tgid: {}\n", self.gid)?;
        write!(
            f,
            "\tgname: {}\n",
            &String::from_utf8_lossy(self.gname.as_slice())
        )?;
        fmt_option_str(f, "hdrcharset", &self.hdrcharset)?;
        write!(
            f,
            "\tlinkpath: {}\n",
            &String::from_utf8_lossy(self.linkpath.as_slice())
        )?;
        write!(f, "\tmtime: {}\n", self.mtime)?;
        write!(
            f,
            "\tpath: {}\n",
            &String::from_utf8_lossy(self.path.as_slice())
        )?;
        write!(f, "\tsize: {}\n", self.size)?;
        write!(f, "\tuid: {}\n", self.uid)?;
        write!(
            f,
            "\tuname: {}\n",
            &String::from_utf8_lossy(self.uname.as_slice())
        )?;
        write!(f, "}}")
    }
}

impl Metadata {
    pub fn empty() -> Self {
        Self {
            atime: None,
            charset: None,
            comment: None,
            gid: 0,
            gname: Vec::new(),
            hdrcharset: None,
            linkpath: Vec::new(),
            mtime: 0.0,
            path: Vec::new(),
            size: 0,
            uid: 0,
            uname: Vec::new(),

            typeflag: FileType::File,
            mode: 0,
            devmajor: 0,
            devminor: 0,
        }
    }

    pub fn new(typeflag: FileType, path: Vec<u8>) -> Self {
        let mut meta = Self::empty();
        meta.typeflag = typeflag;
        meta.path = path;
        meta
    }

    pub fn new_file(path: Vec<u8>, size: u64) -> Self {
        let mut meta = Self::new(FileType::File, path);
        meta.size = size;
        meta
    }

    pub fn new_hardlink(path: Vec<u8>, linkpath: Vec<u8>) -> Self {
        let mut meta = Self::new(FileType::Hardlink, path);
        meta.linkpath = linkpath;
        meta
    }

    pub fn new_symlink(path: Vec<u8>, linkpath: Vec<u8>) -> Self {
        let mut meta = Self::new(FileType::Symlink, path);
        meta.linkpath = linkpath;
        meta
    }

    pub fn new_directory(path: Vec<u8>) -> Self {
        Self::new(FileType::Directory, path)
    }

    pub fn new_chardev(path: Vec<u8>, devmajor: u32, devminor: u32) -> Self {
        let mut meta = Self::new(FileType::Chardev, path);
        meta.devmajor = devmajor;
        meta.devminor = devminor;
        meta
    }

    pub fn new_blockdev(path: Vec<u8>, devmajor: u32, devminor: u32) -> Self {
        let mut meta = Self::new(FileType::Blockdev, path);
        meta.devmajor = devmajor;
        meta.devminor = devminor;
        meta
    }

    pub fn new_fifo(path: Vec<u8>) -> Self {
        Self::new(FileType::Fifo, path)
    }
}

pub fn write_pax_bytes<W: Write>(w: &mut W, key: &[u8], val: &[u8]) -> Result<()> {
    let size = 1 + key.len() + 1 + val.len() + 1; // ' ' key '=' val '\n'
    let mut digit_count = util::log10_ceil(size);
    if util::log10_ceil(size + digit_count) > digit_count {
        digit_count += 1;
    }

    write!(w, "{} ", digit_count + size)?;
    w.write(key)?;
    w.write(b"=")?;
    w.write(val)?;
    w.write(b"\n")?;

    Ok(())
}

pub fn write_pax_u64<W: Write>(w: &mut W, key: &[u8], val: u64) -> Result<()> {
    write_pax_bytes(w, key, val.to_string().as_bytes())
}

pub fn write_pax_time<W: Write>(w: &mut W, key: &[u8], val: f64) -> Result<()> {
    write_pax_bytes(w, key, val.to_string().as_bytes())
}

fn write_header_block<W: Write>(w: &mut W, block: &mut Block) -> Result<()> {
    block_write_str(block, UST_CHKSUM, &[b' '; UST_CHKSUM.length]);
    block_write_str(block, UST_MAGIC, b"ustar");
    block_write_str(block, UST_VERSION, b"00");

    let sum = block.iter().map(|x| *x as u64).sum();
    block_write_octal(block, UST_CHKSUM, sum);

    w.write_all(block)?;
    Ok(())
}

pub fn write_content<W: Write, R: Read>(w: &mut W, r: &mut R, mut count: u64) -> Result<()> {
    let mut block = [0u8; size_of::<Block>()];

    while count >= size_of::<Block>() as u64 {
        r.read_exact(&mut block)?;
        w.write_all(&block)?;
        count -= size_of::<Block>() as u64;
    }

    if count > 0 {
        r.read_exact(&mut block[0..(count as usize)])?;
        block[(count as usize)..].fill(0);
        w.write_all(&block)?;
    }

    Ok(())
}

pub fn read_content<W: Write, R: Read>(w: &mut W, r: &mut R, mut count: u64) -> Result<()> {
    let mut block = [0u8; size_of::<Block>()];

    while count >= size_of::<Block>() as u64 {
        r.read_exact(&mut block)?;
        w.write_all(&block)?;
        count -= size_of::<Block>() as u64;
    }

    if count > 0 {
        r.read_exact(&mut block)?;
        w.write_all(&block[0..(count as usize)])?;
    }

    Ok(())
}

pub fn write_header<'a, W: Write>(w: &mut W, meta: &Metadata) -> Result<()> {
    // We ignore all 'write_pax_*' errors here, since those functions will only fail if
    // writing to the underlying stream fails, and that won't happen when writing to a Vec
    let mut pax_str = Vec::<u8>::new();

    if let Some(atime) = meta.atime {
        let _ = write_pax_time(&mut pax_str, b"atime", atime);
    }

    if let Some(charset) = &meta.charset {
        let _ = write_pax_bytes(&mut pax_str, b"charset", charset.as_slice());
    }

    if let Some(comment) = &meta.comment {
        let _ = write_pax_bytes(&mut pax_str, b"comment", comment.as_slice());
    }

    if meta.gid > 0o7777777u64 {
        let _ = write_pax_u64(&mut pax_str, b"gid", meta.gid);
    }

    if meta.gname.len() > 32 {
        let _ = write_pax_bytes(&mut pax_str, b"gname", meta.gname.as_slice());
    }

    if let Some(hdrcharset) = &meta.hdrcharset {
        let _ = write_pax_bytes(&mut pax_str, b"hdrcharset", hdrcharset.as_slice());
    }

    if meta.linkpath.len() > 100 {
        let _ = write_pax_bytes(&mut pax_str, b"linkpath", meta.linkpath.as_slice());
    }

    if meta.mtime > 0o777777777777u64 as f64 || meta.mtime != meta.mtime.floor() {
        let _ = write_pax_time(&mut pax_str, b"mtime", meta.mtime);
    }

    if meta.path.len() > 100 {
        let _ = write_pax_bytes(&mut pax_str, b"path", meta.path.as_slice());
    }

    if meta.size > 0o77777777777 {
        let _ = write_pax_u64(&mut pax_str, b"size", meta.size);
    }

    if meta.uid > 0o7777777 {
        let _ = write_pax_u64(&mut pax_str, b"uid", meta.uid);
    }

    if meta.uname.len() > 32 {
        let _ = write_pax_bytes(&mut pax_str, b"uname", meta.uname.as_slice());
    }

    if !pax_str.is_empty() {
        write_raw_entry(w, MetaType::PaxNext.char(), &pax_str)?;
    }

    let mut block = new_block();
    block_write_str(&mut block, UST_NAME, meta.path.as_slice());
    block_write_octal(&mut block, UST_MODE, meta.mode as u64);
    block_write_octal(&mut block, UST_UID, meta.uid);
    block_write_octal(&mut block, UST_GID, meta.gid);
    block_write_octal(&mut block, UST_SIZE, meta.size);
    block_write_octal(&mut block, UST_MTIME, meta.mtime.floor() as u64);
    block[UST_TYPEFLAG.start] = meta.typeflag.char();
    block_write_str(&mut block, UST_LINKNAME, meta.linkpath.as_slice());
    block_write_str(&mut block, UST_UNAME, meta.uname.as_slice());
    block_write_str(&mut block, UST_GNAME, meta.gname.as_slice());
    block_write_octal(&mut block, UST_DEVMAJOR, meta.devmajor as u64);
    block_write_octal(&mut block, UST_DEVMINOR, meta.devminor as u64);
    write_header_block(w, &mut block)?;

    Ok(())
}

pub fn write_file<W: Write, R: Read>(w: &mut W, r: &mut R, meta: &Metadata) -> Result<()> {
    write_header(w, meta)?;
    write_content(w, r, meta.size)
}

pub fn write_raw_entry<W: Write>(w: &mut W, typeflag: u8, content: &Vec<u8>) -> Result<()> {
    let mut block = new_block();
    block[UST_TYPEFLAG.start] = typeflag;
    block_write_octal(&mut block, UST_SIZE, content.len() as u64);
    write_header_block(w, &mut block)?;
    write_content(w, &mut content.as_slice(), content.len() as u64)
}

#[derive(Debug, Clone)]
pub struct PaxMeta {
    pub atime: Option<f64>,
    pub charset: Option<Vec<u8>>,
    pub comment: Option<Vec<u8>>,
    pub gid: Option<u64>,
    pub gname: Option<Vec<u8>>,
    pub hdrcharset: Option<Vec<u8>>,
    pub linkpath: Option<Vec<u8>>,
    pub mtime: Option<f64>,
    pub path: Option<Vec<u8>>,
    pub size: Option<u64>,
    pub uid: Option<u64>,
    pub uname: Option<Vec<u8>>,
}

impl PaxMeta {
    pub fn new() -> Self {
        Self {
            atime: None,
            charset: None,
            comment: None,
            gid: None,
            gname: None,
            hdrcharset: None,
            linkpath: None,
            mtime: None,
            path: None,
            size: None,
            uid: None,
            uname: None,
        }
    }

    pub fn parse<R: Read>(&mut self, r: &mut R) -> Result<()> {
        let mut key = Vec::<u8>::new();

        loop {
            let mut size = 0isize;
            let mut num_digits = 0isize;
            let mut buf = [0u8; 1];
            loop {
                if r.read(&mut buf)? == 0 {
                    return Ok(());
                }

                let ch = buf[0];
                if ch == b' ' {
                    break;
                } else if ch < b'0' || ch > b'9' {
                    return Err(anyhow!("Invalid pax field"));
                }

                size = size.wrapping_mul(10);
                size = size.wrapping_add((ch - b'0') as isize);
                num_digits = num_digits.wrapping_add(1);
            }

            let mut remaining_size = size - num_digits - 1;
            key.clear();
            loop {
                if remaining_size < 1 {
                    return Err(anyhow!("Invalid pax field: Unexpected EOF"));
                }

                if r.read(&mut buf)? == 0 {
                    return Err(anyhow!("Invalid pax field: Unexpected EOF"));
                }

                remaining_size -= 1;
                let ch = buf[0];
                if ch == b'=' {
                    break;
                }

                key.push(ch);
            }

            if remaining_size > 16 * 1024 {
                return Err(anyhow!("Too large pax field"));
            } else if remaining_size < 1 {
                return Err(anyhow!("Too small pax field"));
            }

            let mut val = Vec::<u8>::new();
            val.resize((remaining_size - 1) as usize, 0);
            r.read_exact(&mut val.as_mut_slice())?;

            if r.read(&mut buf)? == 0 {
                return Err(anyhow!("Invalid pax field: Unexpected EOF"));
            }
            if buf[0] != b'\n' {
                return Err(anyhow!("Invalid pax field: Expected newline"));
            }

            if key == b"atime" {
                self.atime = Some(std::str::from_utf8(val.as_slice())?.parse()?);
            } else if key == b"charset" {
                self.charset = Some(val);
            } else if key == b"comment" {
                self.comment = Some(val);
            } else if key == b"gid" {
                self.gid = Some(std::str::from_utf8(val.as_slice())?.parse()?);
            } else if key == b"gname" {
                self.gname = Some(val);
            } else if key == b"hdrcharset" {
                self.hdrcharset = Some(val);
            } else if key == b"linkpath" {
                self.linkpath = Some(val);
            } else if key == b"mtime" {
                self.mtime = Some(std::str::from_utf8(val.as_slice())?.parse()?);
            } else if key == b"path" {
                self.path = Some(val);
            } else if key == b"size" {
                self.size = Some(std::str::from_utf8(val.as_slice())?.parse()?);
            } else if key == b"uid" {
                self.uid = Some(std::str::from_utf8(val.as_slice())?.parse()?);
            } else if key == b"uname" {
                self.uname = Some(val);
            } else {
                eprintln!(
                    "Warning: Unknown pax key: {}",
                    String::from_utf8_lossy(key.as_slice())
                );
            }
        }
    }

    pub fn stringify(&self) -> Vec<u8> {
        // We ignore all 'write_pax_*' errors here, since those functions will only fail if
        // writing to the underlying stream fails, and that won't happen when writing to a Vec
        let mut s = Vec::<u8>::new();

        if let Some(atime) = self.atime {
            let _ = write_pax_time(&mut s, b"atime", atime);
        }

        if let Some(charset) = &self.charset {
            let _ = write_pax_bytes(&mut s, b"charset", charset.as_slice());
        }

        if let Some(comment) = &self.comment {
            let _ = write_pax_bytes(&mut s, b"comment", comment.as_slice());
        }

        if let Some(gid) = self.gid {
            let _ = write_pax_u64(&mut s, b"gid", gid);
        }

        if let Some(gname) = &self.gname {
            let _ = write_pax_bytes(&mut s, b"gname", gname.as_slice());
        }

        if let Some(hdrcharset) = &self.hdrcharset {
            let _ = write_pax_bytes(&mut s, b"hdrcharset", hdrcharset.as_slice());
        }

        if let Some(linkpath) = &self.linkpath {
            let _ = write_pax_bytes(&mut s, b"linkpath", linkpath.as_slice());
        }

        if let Some(mtime) = self.mtime {
            let _ = write_pax_time(&mut s, b"mtime", mtime);
        }

        if let Some(path) = &self.path {
            let _ = write_pax_bytes(&mut s, b"path", path.as_slice());
        }

        if let Some(size) = self.size {
            let _ = write_pax_u64(&mut s, b"size", size);
        }

        if let Some(uid) = self.uid {
            let _ = write_pax_u64(&mut s, b"uid", uid);
        }

        if let Some(uname) = &self.uname {
            let _ = write_pax_bytes(&mut s, b"uname", uname.as_slice());
        }

        s
    }
}

pub struct PaxReader<R: Read> {
    r: R,
    pub global_meta: PaxMeta,
}

impl<R: Read> PaxReader<R> {
    pub fn new(r: R) -> Self {
        Self {
            r,
            global_meta: PaxMeta::new(),
        }
    }

    pub fn next_header(&mut self) -> Result<Option<Metadata>> {
        let mut next_meta = PaxMeta::new();
        let mut block = [0u8; size_of::<Block>()];

        loop {
            self.r.read_exact(&mut block)?;
            let typeflag = match MetaType::from_char(block[UST_TYPEFLAG.start]) {
                Some(tf) => tf,
                None => break,
            };

            let size = block_read_size(&block, UST_SIZE);
            let mut content = Vec::<u8>::new();
            read_content(&mut content, &mut self.r, size)?;

            match typeflag {
                MetaType::PaxNext => next_meta.parse(&mut content.as_slice())?,
                MetaType::PaxGlobal => self.global_meta.parse(&mut content.as_slice())?,
                MetaType::GnuPath => next_meta.path = Some(content),
                MetaType::GnuLinkPath => next_meta.linkpath = Some(content),
            }
        }

        if block.iter().all(|x| *x == 0) {
            self.r.read_exact(&mut block)?;
            if block.iter().all(|x| *x == 0) {
                return Ok(None);
            } else {
                return Err(anyhow!("Incomplete end marker"));
            }
        }

        let typeflag = FileType::from_char(block[UST_TYPEFLAG.start]);
        let mut meta = Metadata::empty();
        meta.typeflag = typeflag;
        meta.mode = block_read_octal(&block, UST_MODE) as u32;
        meta.devmajor = block_read_octal(&block, UST_DEVMAJOR) as u32;
        meta.devminor = block_read_octal(&block, UST_DEVMINOR) as u32;

        if let Some(atime) = next_meta.atime {
            meta.atime = Some(atime);
        } else if let Some(atime) = self.global_meta.atime {
            meta.atime = Some(atime);
        }

        if let Some(charset) = next_meta.charset {
            meta.charset = Some(charset);
        } else if let Some(charset) = &self.global_meta.charset {
            meta.charset = Some(charset.clone());
        }

        if let Some(comment) = next_meta.comment {
            meta.comment = Some(comment);
        } else if let Some(comment) = &self.global_meta.comment {
            meta.comment = Some(comment.clone());
        }

        if let Some(gid) = next_meta.gid {
            meta.gid = gid;
        } else if let Some(gid) = self.global_meta.gid {
            meta.gid = gid;
        } else {
            meta.gid = block_read_octal(&block, UST_GID);
        }

        if let Some(gname) = next_meta.gname {
            meta.gname = gname;
        } else if let Some(gname) = &self.global_meta.gname {
            meta.gname = gname.clone();
        } else {
            meta.gname = block_read_str(&block, UST_GNAME);
        }

        if let Some(hdrcharset) = next_meta.hdrcharset {
            meta.hdrcharset = Some(hdrcharset);
        } else if let Some(hdrcharset) = &self.global_meta.hdrcharset {
            meta.hdrcharset = Some(hdrcharset.clone());
        }

        if let Some(linkpath) = next_meta.linkpath {
            meta.linkpath = linkpath;
        } else if let Some(linkpath) = &self.global_meta.linkpath {
            meta.linkpath = linkpath.clone();
        } else if block[UST_LINKNAME.start] != b'\0' {
            meta.linkpath = block_read_path(&block, UST_LINKNAME);
        }

        if let Some(mtime) = next_meta.mtime {
            meta.mtime = mtime;
        } else if let Some(mtime) = self.global_meta.mtime {
            meta.mtime = mtime;
        } else {
            meta.mtime = block_read_octal(&block, UST_MTIME) as f64;
        }

        if let Some(path) = next_meta.path {
            meta.path = path;
        } else if let Some(path) = &self.global_meta.path {
            meta.path = path.clone();
        } else {
            meta.path = block_read_path(&block, UST_NAME);
        }

        if let Some(size) = next_meta.size {
            meta.size = size;
        } else if let Some(size) = self.global_meta.size {
            meta.size = size;
        } else {
            meta.size = block_read_size(&block, UST_SIZE);
        }

        if let Some(uid) = next_meta.uid {
            meta.uid = uid;
        } else if let Some(uid) = self.global_meta.uid {
            meta.uid = uid;
        } else {
            meta.uid = block_read_octal(&block, UST_UID);
        }

        if let Some(uname) = next_meta.uname {
            meta.uname = uname;
        } else if let Some(uname) = &self.global_meta.uname {
            meta.uname = uname.clone();
        } else {
            meta.uname = block_read_str(&block, UST_UNAME);
        }

        Ok(Some(meta))
    }

    pub fn read_content<W: Write>(&mut self, w: &mut W, size: u64) -> Result<()> {
        read_content(w, &mut self.r, size)
    }

    pub fn read_block(&mut self, block: &mut Block) -> Result<()> {
        self.r.read_exact(block)?;
        Ok(())
    }
}
