use libc;
use scar::pax;
use scar::write::ScarWriter;
use std::collections::HashMap;
use std::ffi::OsString;
use std::fs::{self, File};
use std::io::Write;
use std::mem::MaybeUninit;
use std::os::unix::fs::{FileTypeExt, MetadataExt};
use std::path::PathBuf;
use anyhow::Result;

use crate::osstr_as_bytes;
use crate::Compression;

// The platform-specific login in these 'major_minor' functions are taken from here:
// https://go.dev/src/archive/tar/stat_unix.go

#[cfg(any(target_os = "linux", target_os = "android"))]
fn major_minor(dev: u64) -> (u32, u32) {
    (
        (((dev & 0x00000000000fff00) >> 8) | ((dev & 0xfffff00000000000) >> 32)) as u32,
        (((dev & 0x00000000000000ff) >> 0) | ((dev & 0x00000ffffff00000) >> 12)) as u32,
    )
}

#[cfg(any(target_os = "macos", target_os = "ios"))]
fn major_minor(dev: u64) -> (u32, u32) {
    (((dev >> 24) & 0xff) as u32, (dev & 0xffffff) as u32)
}

#[cfg(any(target_os = "freebsd", target_os = "dragonfly"))]
fn major_minor(dev: u64) -> (u32, u32) {
    (((dev >> 8) & 0xff) as u32, (dev & 0xffff00ff) as u32)
}

#[cfg(any(target_os = "netbsd", target_os = "openbsd"))]
fn major_minor(dev: u64) -> (u32, u32) {
    (
        ((dev & 0x0000ff00) >> 8) as u32,
        (((dev & 0x000000ff) >> 0) | ((dev & 0xffff0000) >> 8)) as u32,
    )
}

#[cfg(not(any(
    target_os = "linux",
    target_os = "android",
    target_os = "macos",
    target_os = "ios",
    target_os = "freebsd",
    target_os = "dragonfly",
    target_os = "netbsd",
    target_os = "openbsd"
)))]
fn major_minor(dev: u64) -> (u32, u32) {
    eprintln!("Warning: Populating major/minor fields as 0/0 due to unsupported platform");
    (0, 0)
}

fn username_from_uid(uid: u64) -> Vec<u8> {
    let mut pw = MaybeUninit::uninit();
    let mut name = [0 as libc::c_char; 512];
    let mut result = std::ptr::null_mut();
    unsafe {
        libc::getpwuid_r(
            uid as libc::uid_t,
            pw.as_mut_ptr(),
            &mut name[0],
            512,
            &mut result,
        )
    };

    // Since 'name' is zero initialized, we just have an empty string if getpwuid_r failed
    let len = unsafe { libc::strlen(&name[0]) };
    name[..len].iter().take(len).map(|x| *x as u8).collect()
}

fn groupname_from_gid(gid: u64) -> Vec<u8> {
    let mut grp = MaybeUninit::uninit();
    let mut name = [0 as libc::c_char; 512];
    let mut result = std::ptr::null_mut();
    unsafe {
        libc::getgrgid_r(
            gid as libc::uid_t,
            grp.as_mut_ptr(),
            &mut name[0],
            512,
            &mut result,
        )
    };

    // Since 'name' is zero initialized, we just have an empty string if getgrgid_r failed
    let len = unsafe { libc::strlen(&name[0]) };
    name[..len].iter().map(|x| *x as u8).collect()
}

fn archive_recursive(
    w: &mut ScarWriter,
    path: &PathBuf,
    meta: fs::Metadata,
    hardlinks: &mut HashMap<(u64, u64), PathBuf>,
) -> Result<()> {
    let mut pax_meta = pax::Metadata {
        typeflag: pax::FileType::Unknown,
        mode: meta.mode() & 0o777,
        devmajor: 0,
        devminor: 0,

        atime: None,
        charset: None,
        comment: None,
        gid: meta.gid() as u64,
        gname: groupname_from_gid(meta.gid() as u64),
        hdrcharset: None,
        linkpath: Vec::new(),
        mtime: meta.mtime() as f64,
        path: osstr_as_bytes(path.as_os_str()).to_vec(),
        size: 0,
        uid: meta.uid() as u64,
        uname: username_from_uid(meta.uid() as u64),
    };

    let ftype = meta.file_type();
    if ftype.is_dir() {
        pax_meta.typeflag = pax::FileType::Directory;
        pax_meta.path.push(b'/');
    } else if ftype.is_symlink() {
        pax_meta.typeflag = pax::FileType::Symlink;
        pax_meta.linkpath = osstr_as_bytes(fs::read_link(&path)?.as_os_str()).to_vec();
    } else if ftype.is_file() {
        if meta.nlink() == 1 {
            pax_meta.typeflag = pax::FileType::File;
            pax_meta.size = meta.len();
        } else if let Some(path) = hardlinks.get(&(meta.dev(), meta.ino())) {
            pax_meta.typeflag = pax::FileType::Hardlink;
            pax_meta.linkpath = osstr_as_bytes(path.as_os_str()).to_vec();
        } else {
            pax_meta.typeflag = pax::FileType::File;
            pax_meta.size = meta.len();
            hardlinks.insert((meta.dev(), meta.ino()), path.clone());
        }
    } else if ftype.is_block_device() {
        pax_meta.typeflag = pax::FileType::Blockdev;
        let (maj, min) = major_minor(meta.dev());
        pax_meta.devmajor = maj;
        pax_meta.devminor = min;
    } else if ftype.is_char_device() {
        pax_meta.typeflag = pax::FileType::Chardev;
        let (maj, min) = major_minor(meta.dev());
        pax_meta.devmajor = maj;
        pax_meta.devminor = min;
    } else if ftype.is_fifo() {
        pax_meta.typeflag = pax::FileType::Fifo;
    } else {
        eprintln!("Warning: Encountered directory entry of unknown type, skipping");
        return Ok(());
    };

    if pax_meta.typeflag == pax::FileType::File {
        let mut f = File::open(&path)?;
        w.add_file(&mut f, &pax_meta)?;
    } else if pax_meta.typeflag == pax::FileType::Directory {
        w.add_entry(&pax_meta)?;
        let mut path = path.clone();
        for ent in fs::read_dir(&path)? {
            let ent = ent?;
            path.push(&ent.file_name());
            archive_recursive(w, &path, ent.metadata()?, hardlinks)?;
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
) -> Result<()> {
    let cf = comp.create_compressor_factory();
    let mut writer = ScarWriter::new(cf, ofile)?;
    let mut hardlinks = HashMap::new();

    for arg in args {
        let path: PathBuf = arg.into();
        let meta = fs::metadata(&path)?;
        archive_recursive(&mut writer, &arg.into(), meta, &mut hardlinks)?;
    }

    writer.finish()?;

    Ok(())
}
