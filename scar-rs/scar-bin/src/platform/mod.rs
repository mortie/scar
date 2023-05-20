use std::ffi::OsString;
use std::io::Write;
use std::error::Error;
use crate::Compression;

#[cfg(unix)]
mod unix;
#[cfg(unix)]
pub use unix::cmd_create;

#[cfg(not(unix))]
pub fn cmd_create(
    _ofile: Box<dyn Write>,
    _args: &[OsString],
    _comp: Compression,
) -> Result<(), Box<dyn Error>> {
    Err("Creating archives is not supported on this platform".into())
}
