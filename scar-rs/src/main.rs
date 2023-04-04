use scar::compression;
use scar::pax::{self, PaxReader};
use scar::write::ScarWriter;
use std::error::Error;
use std::io::{self, Read, Write};

fn main() -> Result<(), Box<dyn Error>> {
    let mut reader = PaxReader::new(io::stdin());
    let mut writer = ScarWriter::new(
        Box::new(compression::GzipCompressorFactory::new(6)),
        Box::new(io::stdout()),
    );

    while let Some(meta) = reader.next_header()? {
        writer.add_entry(&meta)?;

        if meta.typeflag == pax::FileType::File {
            let mut block = pax::new_block();
            let mut size = meta.size as i64;
            while size > 0 {
                reader.r.read_exact(&mut block)?;
                writer.w.write_all(&mut block)?;
                size -= block.len() as i64;
            }
        }
    }

    writer.finish()?;

    Ok(())
}
