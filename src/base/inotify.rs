use std::ffi::CString;
use std::io;
use std::os::fd::{AsRawFd, FromRawFd, OwnedFd};
use std::path::Path;

use async_io::Async;

/// A non-blocking inotify descriptor, readable through `async_io`.
pub fn open() -> io::Result<Async<OwnedFd>> {
    // SAFETY: plain syscall; the fd is owned from here on.
    let raw = unsafe { libc::inotify_init1(libc::IN_NONBLOCK | libc::IN_CLOEXEC) };
    if raw < 0 {
        return Err(io::Error::last_os_error());
    }
    // SAFETY: `raw` is a fresh descriptor nothing else owns.
    Async::new(unsafe { OwnedFd::from_raw_fd(raw) })
}

pub fn add_watch(fd: &Async<OwnedFd>, path: &Path, mask: u32) -> io::Result<()> {
    let path = CString::new(path.as_os_str().as_encoded_bytes()).map_err(io::Error::other)?;
    // SAFETY: valid fd and NUL-terminated path.
    if unsafe { libc::inotify_add_watch(fd.as_raw_fd(), path.as_ptr(), mask) } < 0 {
        return Err(io::Error::last_os_error());
    }
    Ok(())
}

/// Drains pending events and returns the names they carry (empty for a watch on a file itself).
pub fn drain(fd: &Async<OwnedFd>) -> Vec<Vec<u8>> {
    const HEADER: usize = std::mem::size_of::<libc::inotify_event>();
    let mut names = Vec::new();
    let mut buffer = [0u8; 4096];
    loop {
        // SAFETY: reads into a buffer of the given length.
        let n = unsafe { libc::read(fd.as_raw_fd(), buffer.as_mut_ptr().cast(), buffer.len()) };
        if n <= 0 {
            return names;
        }
        let events = &buffer[..n as usize];
        let mut offset = 0;
        while offset + HEADER <= events.len() {
            let len = u32::from_ne_bytes(events[offset + 12..offset + 16].try_into().unwrap_or_default()) as usize;
            let end = (offset + HEADER + len).min(events.len());
            let name = &events[offset + HEADER..end];
            names.push(name[..name.iter().position(|&b| b == 0).unwrap_or(name.len())].to_vec());
            offset += HEADER + len;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn inotify_reports_names() {
        let dir = tempfile::tempdir().unwrap();
        let fd = open().unwrap();
        add_watch(&fd, dir.path(), libc::IN_CLOSE_WRITE).unwrap();
        assert!(drain(&fd).is_empty());
        std::fs::write(dir.path().join("a.json"), "x").unwrap();
        // A child being spawned on another test thread can hold the file until it execs,
        // which delays the close event past our own close.
        let mut names = Vec::new();
        for _ in 0..50 {
            names.extend(drain(&fd));
            if !names.is_empty() {
                break;
            }
            std::thread::sleep(std::time::Duration::from_millis(2));
        }
        assert_eq!(names, vec![b"a.json".to_vec()]);
    }
}
