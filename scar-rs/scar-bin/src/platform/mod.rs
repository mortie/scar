#[cfg(unix)]
mod unix;
#[cfg(unix)]
pub use unix::cmd_create;

#[cfg(not(unix))]
pub fn cmd_create(
    ofile: Box<dyn Write>,
    args: &[OsString],
    comp: Compression,
) -> Result<(), Box<dyn Error>> {
    Err("Creating archives is not supported on this platform".into())
}
