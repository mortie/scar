use std::ffi::OsString;
use std::fs::{self, File};
use std::io::Write;
use std::error::Error;
use std::time::UNIX_EPOCH;
use scar::pax;
use scar::write::ScarWriter;
use std::path::PathBuf;

use crate::{Compression, osstr_as_bytes};

fn meta_to_mode(meta: &fs::Metadata) -> u32 {
    if meta.permissions().readonly() {
        0o555
    } else {
        0o755
    }
}

fn normalize_path(path: &[u8]) -> Vec<u8> {
    let mut v = Vec::<u8>::new();
    for ch in path {
        if *ch == b'\\' {
            v.push(b'/');
        } else {
            v.push(*ch);
        }
    }
    v
}

fn archive_recursive(w: &mut ScarWriter, path: &PathBuf, meta: fs::Metadata) -> Result<(), Box<dyn Error>> {
    let mut pax_meta = pax::Metadata {
        typeflag: pax::FileType::Unknown,
        mode: meta_to_mode(&meta),
        devmajor: 0,
        devminor: 0,

        atime: None,
        charset: None,
        comment: None,
        gid: 0,
        gname: Vec::new(),
        hdrcharset: None,
        linkpath: Vec::new(),
        mtime: 0f64,
        path: normalize_path(osstr_as_bytes(path.as_os_str())),
        size: 0,
        uid: 0,
        uname: Vec::new(),
    };

    if let Ok(t) = meta.modified() {
        if let Ok(duration) = t.duration_since(UNIX_EPOCH) {
            pax_meta.mtime = duration.as_secs() as f64;
        }
    }

    let ftype = meta.file_type();
    if ftype.is_dir() {
        pax_meta.typeflag = pax::FileType::Directory;
        pax_meta.path.push(b'/');
    } else if ftype.is_symlink() {
        pax_meta.typeflag = pax::FileType::Symlink;
        pax_meta.linkpath = osstr_as_bytes(fs::read_link(&path)?.as_os_str()).to_vec();
    } else if ftype.is_file() {
        pax_meta.typeflag = pax::FileType::File;
        pax_meta.size = meta.len();
    } else {
        eprintln!("Warning: Encountered directory entry of unknown type, skipping");
        return Ok(());
    }

    if pax_meta.typeflag == pax::FileType::File {
        let mut f = File::open(&path)?;
        w.add_file(&mut f, &pax_meta)?;
    } else if pax_meta.typeflag == pax::FileType::Directory {
        w.add_entry(&pax_meta)?;
        let mut path = path.clone();
        for ent in fs::read_dir(&path)? {
            let ent = ent?;
            path.push(&ent.file_name());
            archive_recursive(w, &path, ent.metadata()?)?;
            path.pop();
        }
    } else {
        w.add_entry(&pax_meta)?;
    }

    Ok(())
}

pub fn cmd_create(
    ofile: Box<dyn Write>,
    args: &[OsString],
    comp: Compression,
) -> Result<(), Box<dyn Error>> {
    let cf = comp.create_compressor_factory();
    let mut writer = ScarWriter::new(cf, ofile)?;

    for arg in args {
        let path: PathBuf = arg.into();
        let meta = fs::metadata(&path)?;
        archive_recursive(&mut writer, &arg.into(), meta)?;
    }

    writer.finish()?;

    Ok(())
}