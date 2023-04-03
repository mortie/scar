use scar::pax;
use std::error::Error;
use std::io;

fn main() -> Result<(), Box<dyn Error>> {
    let mut reader = pax::PaxReader::new(io::stdin());
    while let Some(meta) = reader.next_header()? {
        println!("Found entry: {}", meta);
        if meta.typeflag == pax::FileType::File {
            reader.read_content(&mut io::sink(), meta.size)?;
        }
    }

    Ok(())
}
