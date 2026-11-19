use std::io::{Read, Write};
use std::os::unix::net::UnixStream;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Output, Stdio};
use std::thread::sleep;
use std::time::{Duration, Instant};

use tempfile::TempDir;

const BIN: &str = env!("CARGO_BIN_EXE_astralia");

fn command(dir: &Path) -> Command {
    let mut cmd = Command::new(BIN);
    cmd.env_clear()
        .env("XDG_RUNTIME_DIR", dir)
        .env("XDG_STATE_HOME", dir)
        .env("HOME", dir)
        .env("WAYLAND_DISPLAY", "test-1")
        .env("ASTRALIA_BACKEND", "wayland")
        .env("ASTRALIA_HEADLESS", "1");
    cmd
}

fn socket(dir: &Path) -> PathBuf {
    dir.join("astralia-test-1.sock")
}

fn wait_until(mut done: impl FnMut() -> bool) -> bool {
    let start = Instant::now();
    while start.elapsed() < Duration::from_secs(2) {
        if done() {
            return true;
        }
        sleep(Duration::from_millis(20));
    }
    false
}

/// Kills the daemon if a test fails before it exits on its own.
struct Daemon(Child);

impl Drop for Daemon {
    fn drop(&mut self) {
        let _ = self.0.kill();
        let _ = self.0.wait();
    }
}

impl Daemon {
    fn wait_exit(&mut self) -> Option<i32> {
        let mut code = None;
        wait_until(|| match self.0.try_wait() {
            Ok(Some(status)) => {
                code = status.code();
                true
            }
            _ => false,
        });
        code
    }
}

fn start(dir: &Path) -> Daemon {
    let child = command(dir).arg("debug").stdout(Stdio::null()).stderr(Stdio::null()).spawn().expect("spawn daemon");
    // The lock is taken before the bind, so a visible socket means the daemon is ready.
    assert!(wait_until(|| UnixStream::connect(socket(dir)).is_ok()), "daemon socket never appeared");
    Daemon(child)
}

fn client(dir: &Path, args: &[&str]) -> (i32, String, String) {
    let Output { status, stdout, stderr } = command(dir).args(args).output().expect("run client");
    (status.code().unwrap_or(-1), String::from_utf8_lossy(&stdout).into(), String::from_utf8_lossy(&stderr).into())
}

#[test]
fn status_help_kill() {
    let dir = TempDir::new().unwrap();
    let mut daemon = start(dir.path());
    let (code, out, _) = client(dir.path(), &["status"]);
    assert_eq!(code, 0);
    assert!(out.starts_with("backend: wayland\ncapabilities: lock idle rain"), "{out}");
    let (_, out, _) = client(dir.path(), &["help"]);
    for verb in ["help", "kill", "status"] {
        assert!(out.contains(&format!("  {verb} ")), "{out}");
    }
    assert_eq!(client(dir.path(), &["kill"]).0, 0);
    assert_eq!(daemon.wait_exit(), Some(0));
    assert!(!socket(dir.path()).exists());
}

#[test]
fn second_instance_refused() {
    let dir = TempDir::new().unwrap();
    let _daemon = start(dir.path());
    let out = command(dir.path()).arg("debug").output().unwrap();
    assert_eq!(out.status.code(), Some(1));
    assert!(String::from_utf8_lossy(&out.stderr).contains("already running"));
    assert!(client(dir.path(), &["status"]).1.starts_with("backend: wayland"));
}

#[test]
fn stale_socket_replaced() {
    let dir = TempDir::new().unwrap();
    std::fs::write(socket(dir.path()), "").unwrap();
    let child = command(dir.path()).arg("debug").stderr(Stdio::null()).spawn().unwrap();
    let _daemon = Daemon(child);
    assert!(wait_until(|| client(dir.path(), &["status"]).1.starts_with("backend: wayland")));
}

#[test]
fn client_without_daemon() {
    let dir = TempDir::new().unwrap();
    let (code, _, err) = client(dir.path(), &["status"]);
    assert_eq!(code, 1);
    assert!(err.starts_with("astralia: no running instance ("), "{err}");
}

#[test]
fn silent_client_then_crlf() {
    let dir = TempDir::new().unwrap();
    let _daemon = start(dir.path());
    drop(UnixStream::connect(socket(dir.path())).unwrap());
    let mut stream = UnixStream::connect(socket(dir.path())).unwrap();
    stream.set_read_timeout(Some(Duration::from_secs(3))).unwrap();
    stream.write_all(b"status\r\n").unwrap();
    let mut reply = String::new();
    stream.read_to_string(&mut reply).unwrap();
    assert!(reply.starts_with("backend: wayland"), "{reply:?}");
}

#[test]
fn no_display_exits() {
    let dir = TempDir::new().unwrap();
    let out = command(dir.path()).env_remove("ASTRALIA_HEADLESS").arg("debug").output().unwrap();
    assert_eq!(out.status.code(), Some(1));
    assert!(String::from_utf8_lossy(&out.stderr).contains("wayland: "));
    assert!(!socket(dir.path()).exists());
}

#[test]
fn sigterm_stops() {
    let dir = TempDir::new().unwrap();
    let mut daemon = start(dir.path());
    // SAFETY: signalling our own child.
    unsafe { libc::kill(daemon.0.id() as libc::pid_t, libc::SIGTERM) };
    assert_eq!(daemon.wait_exit(), Some(0));
    assert!(!socket(dir.path()).exists());
}
