use scar::compression::GzipCompressorFactory;
use scar::write::ScarWriter;
use std::error::Error;
use std::io::{self, Write};

fn main() -> Result<(), Box<dyn Error>> {
    let mut scar_writer = ScarWriter::new(
        Box::new(GzipCompressorFactory::new()),
        Box::new(io::stdout()),
    );
    Ok(())
}
