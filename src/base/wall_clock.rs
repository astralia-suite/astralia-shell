use std::time::Duration;

/// Local broken-down time for `secs` since the epoch.
pub fn local_tm(secs: i64) -> libc::tm {
    let secs = secs as libc::time_t;
    // SAFETY: `tm` is plain data; `localtime_r` only writes into it.
    let mut tm: libc::tm = unsafe { std::mem::zeroed() };
    unsafe { libc::localtime_r(&secs, &mut tm) };
    tm
}

/// Time left until the next whole second in whole milliseconds rounded up, at least 1 ms so a timer never spins.
pub fn until_next_second(subsec_nanos: u32) -> Duration {
    let left_ns = 1_000_000_000 - subsec_nanos.min(999_999_999) as u64;
    Duration::from_millis(left_ns.div_ceil(1_000_000).max(1))
}

/// `tm` through `strftime(pattern)`; empty when it does not fit 64 bytes.
pub fn format_tm(tm: &libc::tm, pattern: &str) -> String {
    let Ok(pattern) = std::ffi::CString::new(pattern) else { return String::new() };
    let mut buf = [0u8; 64];
    // SAFETY: `buf` is writable for its length, `pattern` is NUL-terminated, `tm` is a valid `struct tm`.
    let n = unsafe { libc::strftime(buf.as_mut_ptr().cast(), buf.len(), pattern.as_ptr(), tm) };
    String::from_utf8_lossy(&buf[..n]).into_owned()
}

/// The local broken-down time now.
pub fn now_tm() -> libc::tm {
    let secs = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map_or(0, |d| d.as_secs() as i64);
    local_tm(secs)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;

    #[test]
    fn clock_text_and_next_second() {
        let tm = libc::tm { tm_hour: 7, tm_min: 5, tm_sec: 9, ..unsafe { std::mem::zeroed() } };
        assert_eq!(format_tm(&tm, "%H:%M:%S"), "07:05:09");
        assert_eq!(until_next_second(250_000_000), Duration::from_millis(750));
        assert_eq!(until_next_second(999_999_999), Duration::from_millis(1));
        assert_eq!(until_next_second(0), Duration::from_secs(1), "on the boundary waits a full second");
        assert_eq!(until_next_second(999_000_000), Duration::from_millis(1), "last millisecond waits 1 ms");
        assert_eq!(until_next_second(999_500_000), Duration::from_millis(1), "sub-millisecond remainder rounds up");
        assert_eq!(until_next_second(500_000), Duration::from_secs(1), "just past the boundary rounds up to a full second");
        let tm = libc::tm { tm_year: 126, tm_mon: 8, tm_mday: 27, tm_wday: 0, tm_hour: 12, ..unsafe { std::mem::zeroed() } };
        assert_eq!(format_tm(&tm, "%Y-%m-%d %H"), "2026-09-27 12");
    }
}
