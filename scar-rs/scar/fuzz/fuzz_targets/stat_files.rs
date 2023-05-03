#![no_main]

use libfuzzer_sys::fuzz_target;
use scar::read::ScarReader;
use std::io::Cursor;

fuzz_target!(|data: &[u8]| {
    let cursor = Cursor::new(data.to_vec());
    let mut r = match ScarReader::new(cursor) {
        Ok(r) => r,
        Err(_) => return,
    };

    let index = match r.index() {
        Ok(index) => index,
        Err(_) => return,
    };

    for entry in index {
        let entry = match entry {
            Ok(entry) => entry,
            Err(_) => return,
        };

        let mut pr = match r.read_item(&entry) {
            Ok(pr) => pr,
            Err(_) => return,
        };

        match pr.next_header() {
            Ok(_) => (),
            Err(_) => return,
        };
    }
});
