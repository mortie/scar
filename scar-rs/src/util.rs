use std::ops::{AddAssign, DivAssign};

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
