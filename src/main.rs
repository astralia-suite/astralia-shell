mod app;
mod backend;
mod base;
mod config;
mod modules;
#[allow(dead_code)]
mod render;
#[allow(dead_code)]
mod service;

use std::io::Write;
use std::process::ExitCode;

use app::session::{self, SessionEnv, SessionKind};
use app::shell;
use base::cli::{self, Invocation};
use base::process::{self, Instance};
use base::{ipc, paths};

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args_os().skip(1).map(|a| a.to_string_lossy().into_owned()).collect();
    let invocation = cli::parse(&args);
    match &invocation {
        Invocation::Client(command) if command == "modules" => {
            let _ = std::io::stdout().write_all(shell::report().as_bytes());
            return ExitCode::SUCCESS;
        }
        Invocation::Client(command) => return ipc::run_client(&paths::runtime_path(".sock"), command),
        Invocation::Daemon | Invocation::Debug => {}
    }

    let mesa_only = process::prefer_mesa_egl();
    base::log::init();
    if mesa_only {
        ::log::info!("nvidia driver not loaded: using the mesa egl vendor only");
    }
    render::app_fonts::register();
    match session::detect(&SessionEnv::from_env()) {
        SessionKind::Wayland => {}
        SessionKind::X11 => {
            ::log::error!("the x11 backend is not ported yet");
            return ExitCode::FAILURE;
        }
        SessionKind::None => {
            ::log::error!("no graphical session found: set WAYLAND_DISPLAY or DISPLAY, or ASTRALIA_BACKEND=wayland|x11");
            return ExitCode::FAILURE;
        }
    }
    // Taken before the socket is bound, so a second start never unlinks the live socket.
    let _instance = match Instance::acquire(&paths::runtime_path(".lock")) {
        Ok(instance) => instance,
        Err(e) => {
            ::log::error!("{e}");
            return ExitCode::FAILURE;
        }
    };
    if invocation == Invocation::Daemon {
        process::daemonize();
    }
    process::tune_allocator(2);
    ::log::info!("starting the wayland backend");
    match app::run("wayland", shell::wayland_capabilities()) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            ::log::error!("{e}");
            ExitCode::FAILURE
        }
    }
}
