use std::env;
use std::ffi::{OsString, OsStr};
use std::error::Error;
use std::fs::File;
use std::io::{self, Write};
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
        return None
    }

    if arg.len() == 2 {
        return argv.next();
    }

    return Some(bytes_as_osstr(&arg[2..]).to_owned())
}

fn usage(argv0: &str) {
    println!("Usage: {} tree", argv0);
    println!("       {} cat <paths...>", argv0);
    println!("       {} ls <paths...>", argv0);
    println!("       {} stat <paths...>", argv0);
}

fn main() -> Result<(), Box<dyn Error>> {
    let mut ifile: Option<File> = None;
    let mut ofile: Box<dyn Write> = Box::new(io::stdout());

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
            ifile = Some(File::open(val)?);
        } else if let Some(val) = short_opt(b'o', arg, &mut argv) {
            ofile = Box::new(File::create(val)?);
        } else {
            return Err(format!("Invalid option: {}", arg_os.to_string_lossy()).into());
        }
    }

    if args.len() == 0 {
        usage(&argv0);
        process::exit(1);
    }

    match args[0].to_str() {
        Some("tree") => Err("'tree' is not implemented yet".into()),
        Some("cat") => Err("'cat' is not implemented yet".into()),
        Some("ls") => Err("'ls' is not implemented yet".into()),
        Some("stat") => Err("'stat' is not implemented yet".into()),
        _ => Err(format!("Unknown subcommand: {}", args[0].to_string_lossy()).into()),
    }
}
