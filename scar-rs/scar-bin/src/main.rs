use scar::compression;
use scar::pax::{self, PaxReader};
use scar::read::ScarReader;
use scar::write::ScarWriter;
use std::env;
use std::error::Error;
use std::ffi::{OsStr, OsString};
use std::fs::File;
use std::io::{self, Read, Write};
use std::process;

fn osstr_as_bytes(s: &OsStr) -> &[u8] {
    unsafe { std::mem::transmute(s) }
}

fn bytes_as_osstr(b: &[u8]) -> &OsStr {
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

enum Compression {
    Gzip(u32),
    Plain,
}
impl Compression {
    fn create_compressor_factory(&self) -> Box<dyn compression::CompressorFactory> {
        match self {
            Compression::Gzip(level) => Box::new(compression::GzipCompressorFactory::new(*level)),
            Compression::Plain => Box::new(compression::PlainCompressorFactory::new()),
        }
    }
}

fn cmd_list(ifile: File, mut ofile: Box<dyn Write>) -> Result<(), Box<dyn Error>> {
    let mut reader = ScarReader::new(ifile)?;
    for entry in reader.index()? {
        let entry = entry?;
        write!(ofile, "{}\n", String::from_utf8_lossy(entry.path.as_ref()))?;
    }

    Ok(())
}

fn cmd_convert(
    ifile: Box<dyn Read>,
    ofile: Box<dyn Write>,
    comp: Option<Compression>,
) -> Result<(), Box<dyn Error>> {
    let mut reader = PaxReader::new(ifile);

    let cf = match comp {
        Some(comp) => comp.create_compressor_factory(),
        None => Box::new(compression::GzipCompressorFactory::new(6)),
    };

    let mut writer = ScarWriter::new(cf, ofile);

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
    println!("Usage: {} list", argv0);
    println!("       {} cat <paths...>", argv0);
    println!("       {} ls <paths...>", argv0);
    println!("       {} stat <paths...>", argv0);
    println!("       {} convert", argv0);
}

fn main() -> Result<(), Box<dyn Error>> {
    let mut ifile = IFile::Stdin(io::stdin());
    let mut ofile: Box<dyn Write> = Box::new(io::stdout());
    let mut comp: Option<Compression> = None;
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
            comp = Some(match val {
                Some("gzip") => Compression::Gzip(6),
                Some("plain") => Compression::Plain,
                _ => {
                    return Err(format!("Invalid compression: {}", arg_os.to_string_lossy()).into())
                }
            })
        } else {
            return Err(format!("Invalid option: {}", arg_os.to_string_lossy()).into());
        }
    }

    if args.len() == 0 {
        usage(&argv0);
        process::exit(1);
    }

    match args[0].to_str() {
        Some("list") => {
            if args.len() > 1 {
                return Err("'list' expects no further arguments".into());
            }

            match ifile {
                IFile::File(ifile) => cmd_list(ifile, ofile),
                _ => Err("Input must be a file ('-i')".into()),
            }
        }
        Some("cat") => Err("'cat' is not implemented yet".into()),
        Some("ls") => Err("'ls' is not implemented yet".into()),
        Some("stat") => Err("'stat' is not implemented yet".into()),
        Some("convert") => {
            if args.len() > 1 {
                return Err("'convert' expects no further arguments".into());
            }

            match ifile {
                IFile::File(f) => cmd_convert(Box::new(f), ofile, comp),
                IFile::Stdin(s) => cmd_convert(Box::new(s), ofile, comp),
            }
        }
        _ => Err(format!("Unknown subcommand: {}", args[0].to_string_lossy()).into()),
    }
}
