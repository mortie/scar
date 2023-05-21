use regex::Regex;
use scar::compression;
use scar::pax::{self, PaxReader};
use scar::read::ScarReader;
use scar::write::ScarWriter;
use std::env;
use std::ffi::{OsStr, OsString};
use std::fs::File;
use std::io::{self, Read, Write};
use std::process;
use time;
use anyhow::{Result, anyhow};

mod fnmatch;
use fnmatch::glob_to_regex;

mod platform;
use platform::cmd_create;

pub fn osstr_as_bytes(s: &OsStr) -> &[u8] {
    unsafe { std::mem::transmute(s) }
}

pub fn bytes_as_osstr(b: &[u8]) -> &OsStr {
    unsafe { std::mem::transmute(b) }
}

fn short_opt(opt: u8, arg: &[u8], argv: &mut env::ArgsOs) -> Option<OsString> {
    if arg.len() < 2 {
        return None;
    }

    if arg[0] != b'-' || arg[1] != opt {
        return None;
    }

    if arg.len() == 2 {
        return argv.next();
    }

    return Some(bytes_as_osstr(&arg[2..]).to_owned());
}

enum IFile {
    File(File),
    Stdin(io::Stdin),
}

pub enum Compression {
    Gzip(u32),
    Xz(u32),
    Zstd(u32),
    Plain,
    Auto,
}

impl Compression {
    fn create_compressor_factory(&self) -> Box<dyn compression::CompressorFactory> {
        match self {
            Compression::Gzip(level) => Box::new(compression::GzipCompressorFactory::new(*level)),
            Compression::Xz(level) => Box::new(compression::XzCompressorFactory::new(*level)),
            Compression::Zstd(level) => Box::new(compression::ZstdCompressorFactory::new(*level)),
            Compression::Plain => Box::new(compression::PlainCompressorFactory::new()),
            Compression::Auto => Box::new(compression::ZstdCompressorFactory::new(3)),
        }
    }

    fn create_reader(&self, r: File) -> Result<ScarReader> {
        match self {
            Compression::Gzip(_) => ScarReader::with_decompressor(
                r,
                Box::new(compression::GzipDecompressorFactory::new()),
            ),
            Compression::Xz(_) => ScarReader::with_decompressor(
                r,
                Box::new(compression::XzDecompressorFactory::new()),
            ),
            Compression::Zstd(_) => ScarReader::with_decompressor(
                r,
                Box::new(compression::ZstdDecompressorFactory::new()),
            ),
            Compression::Plain => ScarReader::with_decompressor(
                r,
                Box::new(compression::PlainDecompressorFactory::new()),
            ),
            Compression::Auto => ScarReader::new(r),
        }
    }
}

fn cmd_list(
    ifile: File,
    mut ofile: Box<dyn Write>,
    comp: Compression,
) -> Result<()> {
    let mut reader = comp.create_reader(ifile)?;
    for entry in reader.index()? {
        let entry = entry?;
        write!(ofile, "{}\n", String::from_utf8_lossy(&entry.path))?;
    }

    Ok(())
}

fn cmd_cat(
    ifile: File,
    mut ofile: Box<dyn Write>,
    args: &[OsString],
    comp: Compression,
) -> Result<()> {
    let mut patterns = Vec::<Regex>::new();
    patterns.reserve(args.len());
    for arg in args {
        patterns.push(glob_to_regex(&arg.to_string_lossy())?);
    }

    let mut reader = comp.create_reader(ifile)?;
    for pattern in patterns {
        for entry in reader.index()? {
            let entry = entry?;
            if pattern.is_match(&String::from_utf8_lossy(&entry.path)) {
                let mut pr = reader.read_item(&entry)?;
                let header = match pr.next_header()? {
                    Some(h) => h,
                    None => continue,
                };

                if header.typeflag != pax::FileType::File {
                    continue;
                }

                pr.read_content(&mut ofile, header.size)?;
            }
        }
    }

    Ok(())
}

fn cmd_ls(
    ifile: File,
    mut ofile: Box<dyn Write>,
    args: &[OsString],
    comp: Compression,
) -> Result<()> {
    let mut patterns = Vec::<Regex>::new();
    patterns.reserve(args.len());
    for arg in args {
        let mut s = arg.to_string_lossy();
        patterns.push(glob_to_regex(&s)?);
        s += "/*";
        patterns.push(glob_to_regex(&s)?);
    }

    if patterns.len() == 0 {
        patterns.push(glob_to_regex("/*".into())?);
    }

    let mut reader = comp.create_reader(ifile)?;
    for pattern in patterns {
        for entry in reader.index()? {
            let entry = entry?;
            let s = String::from_utf8_lossy(&entry.path);
            if pattern.is_match(&s) {
                write!(ofile, "{}\n", s)?
            }
        }
    }

    Ok(())
}

fn cmd_stat(
    ifile: File,
    mut ofile: Box<dyn Write>,
    args: &[OsString],
    comp: Compression,
) -> Result<()> {
    let mut patterns = Vec::<Regex>::new();
    patterns.reserve(args.len());
    for arg in args {
        patterns.push(glob_to_regex(&arg.to_string_lossy())?);
    }

    let mut reader = comp.create_reader(ifile)?;
    let mut first = true;
    for pattern in patterns {
        for entry in reader.index()? {
            let entry = entry?;
            if pattern.is_match(&String::from_utf8_lossy(&entry.path)) {
                let mut pr = reader.read_item(&entry)?;
                let header = match pr.next_header()? {
                    Some(h) => h,
                    None => continue,
                };

                if !first {
                    write!(ofile, "\n")?;
                }
                first = false;

                if header.linkpath.len() > 0 {
                    write!(
                        ofile,
                        "  Path: {} -> {}\n",
                        String::from_utf8_lossy(&header.path),
                        String::from_utf8_lossy(&header.linkpath)
                    )
                } else {
                    write!(ofile, "  Path: {}\n", String::from_utf8_lossy(&header.path))
                }?;

                write!(ofile, "  Type: {:?}\n", header.typeflag)?;

                write!(ofile, "  Size: {}\n", header.size)?;

                if header.devmajor != 0 || header.devminor != 0 {
                    write!(ofile, "Device: {}/{}\n", header.devmajor, header.devminor)?;
                }

                if let Some(atime) = header.atime {
                    write!(
                        ofile,
                        " ATime: {}\n",
                        time::OffsetDateTime::from_unix_timestamp_nanos(
                            (atime * 1000000000f64) as i128
                        )?
                    )?;
                }

                write!(
                    ofile,
                    " MTime: {}\n",
                    time::OffsetDateTime::from_unix_timestamp_nanos(
                        (header.mtime * 1000000000f64) as i128
                    )?
                )?;

                write!(
                    ofile,
                    "Access: {:o}  Uid: {}/{}  Gid: {}/{}\n",
                    header.mode,
                    header.uid,
                    String::from_utf8_lossy(&header.uname),
                    header.gid,
                    String::from_utf8_lossy(&header.gname)
                )?;

                if let Some(hdrcharset) = header.hdrcharset {
                    write!(
                        ofile,
                        "Hdrcharset: {}\n",
                        String::from_utf8_lossy(&hdrcharset)
                    )?;
                }

                if let Some(charset) = header.charset {
                    write!(ofile, "   Charset: {}\n", String::from_utf8_lossy(&charset))?;
                }

                if let Some(comment) = header.comment {
                    write!(ofile, "   Comment: {}\n", String::from_utf8_lossy(&comment))?;
                }
            }
        }
    }

    Ok(())
}

fn cmd_convert(
    ifile: Box<dyn Read>,
    ofile: Box<dyn Write>,
    comp: Compression,
) -> Result<()> {
    let mut reader = PaxReader::new(ifile);

    let cf = comp.create_compressor_factory();
    let mut writer = ScarWriter::new(cf, ofile)?;

    let mut block = pax::new_block();
    while let Some(header) = reader.next_header()? {
        writer.add_entry(&header)?;

        if header.typeflag == pax::FileType::File {
            let mut size = header.size as i64;
            while size > 0 {
                reader.read_block(&mut block)?;
                writer.write_block(&block)?;
                size -= block.len() as i64;
            }
        }
    }

    writer.finish()?;

    Ok(())
}

fn usage(argv0: &str) {
    println!("Usage:");
    println!("  {} [options] list", argv0);
    println!("  {} [options] cat <paths...>", argv0);
    println!("  {} [options] ls <paths...>", argv0);
    println!("  {} [options] stat <paths...>", argv0);
    println!("  {} [options] create <paths...>", argv0);
    println!("  {} [options] convert", argv0);
    println!("Options:");
    println!("  -i<path>      Input file (default: stdin for 'convert')");
    println!("  -o<path>      Output file (default: stdout)");
    println!("  -c<format>    Compression format (gzip, xz, zstd, plain, auto) (default: auto)");
    println!("  -h, --help    Print this help text");
    println!("  -v, --version Print version");
}

fn main() -> Result<()> {
    let mut ifile = IFile::Stdin(io::stdin());
    let mut ofile: Box<dyn Write> = Box::new(io::stdout());
    let mut comp = Compression::Auto;
    let mut args = Vec::<OsString>::new();

    let mut argv = env::args_os();
    let argv0 = argv.next().unwrap().to_string_lossy().to_string();
    while let Some(arg_os) = argv.next() {
        let arg = osstr_as_bytes(&arg_os);

        if arg.len() == 0 || arg[0] != b'-' {
            args.push(arg_os);
            continue;
        }

        if let Some(val) = short_opt(b'i', arg, &mut argv) {
            if val.to_str() == Some("-") {
                ifile = IFile::Stdin(io::stdin());
            } else {
                ifile = IFile::File(File::open(val)?);
            }
        } else if let Some(val) = short_opt(b'o', arg, &mut argv) {
            ofile = Box::new(File::create(val)?);
        } else if let Some(val_os) = short_opt(b'c', arg, &mut argv) {
            let val = val_os.to_str();
            comp = match val {
                Some("gzip") => Compression::Gzip(6),
                Some("xz") => Compression::Xz(6),
                Some("zstd") => Compression::Zstd(3),
                Some("plain") => Compression::Plain,
                Some("auto") => Compression::Auto,
                _ => {
                    return Err(anyhow!("Invalid compression: {}", arg_os.to_string_lossy()));
                }
            };
        } else if arg == b"-h" || arg == b"--help" {
            usage(&argv0);
            return Ok(());
        } else if arg == b"-v" || arg == b"--version" {
            println!("Scar {}", env!("CARGO_PKG_VERSION"));
            return Ok(());
        } else {
            return Err(anyhow!("Invalid option: {}", arg_os.to_string_lossy()));
        }
    }

    if args.len() == 0 {
        usage(&argv0);
        process::exit(1);
    }

    let subcmd = args[0].to_str();
    let args = &args[1..];
    match subcmd {
        Some("list") => {
            if args.len() > 1 {
                return Err(anyhow!("'list' expects no further arguments"));
            }

            match ifile {
                IFile::File(ifile) => cmd_list(ifile, ofile, comp),
                _ => Err(anyhow!("Input must be a file ('-i')")),
            }
        }
        Some("cat") => match ifile {
            IFile::File(f) => cmd_cat(f, ofile, args, comp),
            _ => Err(anyhow!("Input must be a file ('-i')")),
        },
        Some("ls") => match ifile {
            IFile::File(f) => cmd_ls(f, ofile, args, comp),
            _ => Err(anyhow!("Input must be a file ('-i')")),
        },
        Some("stat") => match ifile {
            IFile::File(f) => cmd_stat(f, ofile, args, comp),
            _ => Err(anyhow!("Input must be a file ('-i')")),
        },
        Some("create") | Some("c") => cmd_create(ofile, args, comp),
        Some("convert") => {
            if args.len() > 1 {
                return Err(anyhow!("'convert' expects no further arguments"));
            }

            match ifile {
                IFile::File(f) => cmd_convert(Box::new(f), ofile, comp),
                IFile::Stdin(s) => cmd_convert(Box::new(s), ofile, comp),
            }
        }
        _ => Err(anyhow!("Unknown subcommand: {}", args[0].to_string_lossy())),
    }
}
