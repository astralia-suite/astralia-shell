use std::backtrace::Backtrace;
use std::fs::{self, File, OpenOptions};
use std::io::Write;
use std::sync::Mutex;
use std::time::{SystemTime, UNIX_EPOCH};

use ::log::{Level, LevelFilter, Log, Metadata, Record};

use crate::base::{paths, wall_clock};

const MAX_LOG_BYTES: u64 = 1024 * 1024;

struct Logger {
    file: Mutex<Option<File>>,
}

pub fn format_line(stamp: &str, millis: u32, level: &str, message: &str) -> String {
    format!("[{stamp}.{millis:03}] [{level}] {message}\n")
}

fn now() -> (String, u32) {
    let elapsed = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default();
    let tm = wall_clock::local_tm(elapsed.as_secs() as i64);
    let stamp = format!(
        "{:04}-{:02}-{:02} {:02}:{:02}:{:02}",
        tm.tm_year + 1900,
        tm.tm_mon + 1,
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec
    );
    (stamp, elapsed.subsec_millis())
}

/// Everything this crate logs; dependencies only from `warn` up.
fn wanted(target: &str, level: Level) -> bool {
    target.starts_with(env!("CARGO_CRATE_NAME")) || level <= Level::Warn
}

impl Log for Logger {
    fn enabled(&self, metadata: &Metadata) -> bool {
        wanted(metadata.target(), metadata.level())
    }

    fn log(&self, record: &Record) {
        if !self.enabled(record.metadata()) {
            return;
        }
        let (stamp, millis) = now();
        let level = record.level().as_str().to_ascii_lowercase();
        let line = format_line(&stamp, millis, &level, &record.args().to_string());
        let _ = std::io::stderr().write_all(line.as_bytes());
        if let Ok(mut file) = self.file.lock()
            && let Some(file) = file.as_mut()
        {
            let _ = file.write_all(line.as_bytes());
            let _ = file.flush();
        }
    }

    fn flush(&self) {}
}

fn open_file() -> Option<File> {
    let path = paths::log_path();
    if let Some(dir) = path.parent() {
        let _ = fs::create_dir_all(dir);
    }
    if fs::metadata(&path).is_ok_and(|m| m.len() > MAX_LOG_BYTES) {
        let mut rotated = path.clone().into_os_string();
        rotated.push(".1");
        let _ = fs::rename(&path, rotated);
    }
    OpenOptions::new().create(true).append(true).open(&path).ok()
}

/// Installs the stderr + file logger and a panic hook that logs a backtrace.
pub fn init() {
    let logger = Box::leak(Box::new(Logger { file: Mutex::new(open_file()) }));
    if ::log::set_logger(logger).is_ok() {
        ::log::set_max_level(LevelFilter::Debug);
    }
    let default_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |info| {
        ::log::error!("{info}\n{}", Backtrace::force_capture());
        default_hook(info);
    }));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn dependency_chatter_is_filtered() {
        assert!(wanted(concat!(env!("CARGO_CRATE_NAME"), "::service"), Level::Info));
        assert!(!wanted("zbus::connection", Level::Info) && !wanted("zbus::connection", Level::Trace));
        assert!(wanted("zbus::connection", Level::Warn));
    }

    #[test]
    fn line_format() {
        assert_eq!(format_line("2026-11-18 23:44:01", 7, "info", "hi"), "[2026-11-18 23:44:01.007] [info] hi\n");
    }
}
