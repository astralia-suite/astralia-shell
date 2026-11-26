use std::ffi::CStr;
use std::fs;
use std::io::{self, Read, Write};
use std::os::fd::{AsFd, BorrowedFd};
use std::os::unix::net::{UnixListener, UnixStream};
use std::path::{Path, PathBuf};
use std::process::ExitCode;
use std::time::Duration;

use calloop::generic::Generic;
use calloop::{Interest, LoopHandle, Mode, PostAction};

use crate::app::App;
use crate::app::shell::{self, BUILTINS, Host, Shell};

const ERROR_PREFIX: &str = "error: ";
const IO_TIMEOUT: Duration = Duration::from_secs(2);

pub fn dispatch<D: Host + AsMut<App>>(data: &mut D, command: &str) -> String {
    let bound = data.shell().map(Shell::bound_verbs).unwrap_or_default();
    match command {
        "help" => {
            let mut entries: Vec<(&str, &str)> = BUILTINS.iter().map(|b| (b.verb, b.description)).collect();
            entries.extend(bound.iter().map(|v| (v.name(), v.description())));
            shell::help(&entries)
        }
        "kill" => {
            data.as_mut().signal.stop();
            String::new()
        }
        "status" => {
            match data.shell() {
                Some(s) => s.status(data),
                None => shell::status(&[]),
            }
        }
        _ => match bound.into_iter().find(|v| v.name() == command) {
            Some(verb) => {
                Shell::run(data, verb);
                String::new()
            }
            None => {
                ::log::error!("ipc: unknown command '{command}'");
                format!("{ERROR_PREFIX}unknown command '{command}'\n")
            }
        },
    }
}

/// The listening socket; dropping it unlinks the socket file.
pub struct Server {
    listener: UnixListener,
    path: PathBuf,
}

impl Server {
    pub fn bind(path: &Path) -> io::Result<Server> {
        // Only reached with the instance lock held, so this file is stale.
        let _ = fs::remove_file(path);
        let listener = UnixListener::bind(path).map_err(|e| io::Error::new(e.kind(), format!("bind {}: {e}", path.display())))?;
        listener.set_nonblocking(true)?;
        Ok(Server { listener, path: path.to_owned() })
    }

    fn accept_all<D: Host + AsMut<App>>(&self, data: &mut D) {
        loop {
            match self.listener.accept() {
                Ok((stream, _)) => serve(stream, data),
                Err(e) if e.kind() == io::ErrorKind::WouldBlock => return,
                Err(e) => {
                    ::log::error!("ipc: accept: {e}");
                    return;
                }
            }
        }
    }
}

impl AsFd for Server {
    fn as_fd(&self) -> BorrowedFd<'_> {
        self.listener.as_fd()
    }
}

impl Drop for Server {
    fn drop(&mut self) {
        let _ = fs::remove_file(&self.path);
    }
}

fn set_timeouts(stream: &UnixStream) -> io::Result<()> {
    stream.set_read_timeout(Some(IO_TIMEOUT))?;
    stream.set_write_timeout(Some(IO_TIMEOUT))
}

// Blocking read per client; move to a per-client nonblocking source if a slow client ever stalls frames
fn serve<D: Host + AsMut<App>>(mut stream: UnixStream, data: &mut D) {
    if let Err(e) = stream.set_nonblocking(false).and_then(|_| set_timeouts(&stream)) {
        ::log::error!("ipc: client setup: {e}");
        return;
    }
    let mut buf = [0u8; 256];
    let n = match stream.read(&mut buf) {
        Ok(n) if n > 0 => n,
        _ => return,
    };
    let command = String::from_utf8_lossy(&buf[..n]);
    let command = command.trim_end_matches(['\r', '\n']);
    ::log::info!("ipc: {command}");
    let _ = stream.write_all(dispatch(data, command).as_bytes());
}

pub fn insert<D: Host + AsMut<App> + 'static>(handle: &LoopHandle<'static, D>, server: Server) -> io::Result<()> {
    handle
        .insert_source(Generic::new(server, Interest::READ, Mode::Level), |_, server, data: &mut D| {
            server.accept_all(data);
            Ok(PostAction::Continue)
        })
        .map_err(|e| io::Error::other(e.error))?;
    Ok(())
}

/// `strerror` text without Rust's ` (os error N)` suffix.
fn os_message(e: &io::Error) -> String {
    match e.raw_os_error() {
        // SAFETY: strerror returns a valid C string; the client is single-threaded.
        Some(code) => unsafe { CStr::from_ptr(libc::strerror(code)) }.to_string_lossy().into_owned(),
        None => e.to_string(),
    }
}

pub fn run_client(path: &Path, command: &str) -> ExitCode {
    let mut stream = match UnixStream::connect(path) {
        Ok(stream) => stream,
        Err(e) => {
            eprintln!("astralia: no running instance ({}: {})", path.display(), os_message(&e));
            return ExitCode::FAILURE;
        }
    };
    let _ = set_timeouts(&stream);
    let _ = stream.write_all(format!("{command}\n").as_bytes());
    let mut response = Vec::new();
    let _ = stream.read_to_end(&mut response);
    let response = String::from_utf8_lossy(&response);
    if let Some(error) = response.strip_prefix(ERROR_PREFIX) {
        eprint!("astralia: {error}");
        return ExitCode::FAILURE;
    }
    let _ = io::stdout().write_all(response.as_bytes());
    ExitCode::SUCCESS
}

#[cfg(test)]
mod tests {
    use super::*;
    use calloop::EventLoop;

    fn app() -> App {
        let event_loop: EventLoop<App> = EventLoop::try_new().unwrap();
        App { signal: event_loop.get_signal(), services: None }
    }

    #[test]
    fn dispatch_builtins() {
        let mut app = app();
        assert!(dispatch(&mut app, "help").contains("  status  show the backend"));
        assert_eq!(dispatch(&mut app, "bogus"), "error: unknown command 'bogus'\n");
        assert!(dispatch(&mut app, "status").starts_with("backend: wayland\n"));
    }

    struct WithShell {
        app: App,
        shell: Shell<WithShell>,
        open: bool,
    }

    impl Host for WithShell {
        fn shell(&self) -> Option<&Shell<WithShell>> {
            Some(&self.shell)
        }
    }

    impl AsMut<App> for WithShell {
        fn as_mut(&mut self) -> &mut App {
            &mut self.app
        }
    }

    #[test]
    fn shell_verbs_over_ipc() {
        let mut shell = Shell::new();
        shell.bind(crate::app::shell::ShellVerb::LauncherGlobal, |d: &mut WithShell| d.open = !d.open);
        shell.track("launcher", |d: &WithShell| d.open);
        let mut data = WithShell { app: app(), shell, open: false };
        assert_eq!(dispatch(&mut data, "launcher global"), "", "a bound verb answers nothing");
        assert!(data.open, "and runs its binding");
        assert!(dispatch(&mut data, "status").ends_with("open: launcher\n"), "status lists open modules");
        assert!(dispatch(&mut data, "help").contains("launcher global  toggle the launcher, searching from /"));
        assert_eq!(dispatch(&mut data, "logout"), "error: unknown command 'logout'\n", "an unbound verb is unknown");
    }
}
