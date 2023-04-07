use scar::compression;
use scar::pax::{self, PaxReader};
use scar::read::ScarReader;
use scar::write::ScarWriter;
use std::error::Error;
use std::fs;
use std::io;

fn main() -> Result<(), Box<dyn Error>> {
    {
        let mut reader = PaxReader::new(io::stdin());
        let mut writer = ScarWriter::new(
            Box::new(compression::GzipCompressorFactory::new(6)),
            Box::new(fs::File::create("tmp.scar")?),
        );

        while let Some(meta) = reader.next_header()? {
            writer.add_entry(&meta)?;

            if meta.typeflag == pax::FileType::File {
                let mut block = pax::new_block();
                let mut size = meta.size as i64;
                while size > 0 {
                    reader.read_block(&mut block)?;
                    writer.write_block(&block)?;
                    size -= block.len() as i64;
                }
            }
        }

        writer.finish()?;
    }

    {
        let f = Box::new(fs::File::open("tmp.scar")?);
        let df = Box::new(compression::GzipDecompressorFactory::new());
        let mut reader = ScarReader::new(f, df)?;

        for item in reader.index()? {
            println!("{}", item?);
        }
    }

    Ok(())
}
