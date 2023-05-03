use std::io::{self, BufRead, Read, Seek};
use std::ops::{AddAssign, DivAssign};

pub trait ReadSeek: Read + Seek {}
impl<T> ReadSeek for T where T: Read + Seek {}

#[derive(Debug, Clone)]
pub struct Checkpoint {
    pub compressed_loc: u64,
    pub raw_loc: u64,
}

pub fn log10_ceil<T: AddAssign + DivAssign + PartialOrd + From<u8>>(mut num: T) -> T {
    if num == 0.into() {
        return 1.into();
    }

    let mut log = 0.into();
    while num > 0.into() {
        log += 1.into();
        num /= 10.into();
    }

    log
}

pub fn find_last_occurrence(heystack: &[u8], needle: &[u8]) -> Option<usize> {
    if heystack.len() < needle.len() {
        return None;
    }

    let mut idx = heystack.len() - needle.len();

    loop {
        if &heystack[idx..(idx + needle.len())] == needle {
            return Some(idx);
        }

        if idx == 0 {
            return None;
        }

        idx -= 1;
    }
}

pub fn read_num_from_bufread<BR: BufRead>(br: &mut BR) -> io::Result<(u64, usize)> {
    let mut num = 0u64;
    let mut num_len = 0usize;
    loop {
        let buf = br.fill_buf()?;
        if buf.len() == 0 {
            return Ok((num, num_len));
        }

        let mut count = 0;
        for ch in buf {
            if *ch >= b'0' && *ch <= b'9' {
                count += 1;
                num = num.wrapping_mul(10);
                num = num.wrapping_add((*ch - b'0') as u64);
                num_len = num_len.wrapping_add(1);
            } else {
                br.consume(count);
                return Ok((num, num_len));
            }
        }

        br.consume(count);
    }
}

#[test]
fn test_log10_ceil() {
    for x in 0..10 {
        assert_eq!(log10_ceil(x), 1);
    }
    for x in 10..100 {
        assert_eq!(log10_ceil(x), 2);
    }
    for x in 100..1000 {
        assert_eq!(log10_ceil(x), 3);
    }

    assert_eq!(log10_ceil(1500), 4);
    assert_eq!(log10_ceil(15000), 5);
    assert_eq!(log10_ceil(16000usize), 5usize);
}
