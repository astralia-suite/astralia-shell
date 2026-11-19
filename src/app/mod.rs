use std::io;

use calloop::signals::{Signal, Signals};
use calloop::{EventLoop, LoopSignal};

pub mod session;
pub mod shell;

use crate::backend::wayland;
use crate::base::{ipc, paths};
use crate::service::Services;
use shell::Capabilities;

pub struct App {
    pub backend: &'static str,
    pub capabilities: Capabilities,
    pub signal: LoopSignal,
    pub services: Option<Services>,
}

impl shell::Host for App {}

impl AsMut<App> for App {
    fn as_mut(&mut self) -> &mut App {
        self
    }
}

/// Runs the daemon loop until `kill`, `SIGINT` or `SIGTERM`. The IPC socket is unlinked when the loop drops.
/// `ASTRALIA_HEADLESS=1` skips Wayland and serves IPC only.
pub fn run(backend: &'static str, capabilities: Capabilities) -> io::Result<()> {
    if std::env::var("ASTRALIA_HEADLESS").as_deref() == Ok("1") {
        ::log::info!("wayland: headless");
        let event_loop: EventLoop<App> = EventLoop::try_new().map_err(io::Error::other)?;
        let services = start(&event_loop)?;
        let app = App { backend, capabilities, signal: event_loop.get_signal(), services: Some(services) };
        return serve(event_loop, app);
    }
    let event_loop: EventLoop<wayland::State> = EventLoop::try_new().map_err(io::Error::other)?;
    let services = start(&event_loop)?;
    let core = App { backend, capabilities, signal: event_loop.get_signal(), services: Some(services) };
    // Connected before the socket is bound, so a failed start leaves no socket behind.
    let state = wayland::connect(core, &event_loop.handle()).map_err(|e| io::Error::other(format!("wayland: {e}")))?;
    serve(event_loop, state)
}

/// Stops the loop on `SIGINT` and `SIGTERM`, then starts the services.
fn start<D: AsMut<App> + 'static>(event_loop: &EventLoop<'static, D>) -> io::Result<Services> {
    let handle = event_loop.handle();
    let signals = Signals::new(&[Signal::SIGINT, Signal::SIGTERM]).map_err(io::Error::other)?;
    handle
        .insert_source(signals, |event, _, data: &mut D| {
            ::log::info!("received {:?}, stopping", event.signal());
            data.as_mut().signal.stop();
        })
        .map_err(|e| io::Error::other(e.error))?;
    // After the signal mask is set: the threads services start inherit it, so a SIGTERM reaches the loop.
    Services::start(&handle)
}

fn serve<D: shell::Host + AsMut<App> + 'static>(mut event_loop: EventLoop<'static, D>, mut data: D) -> io::Result<()> {
    ipc::insert(&event_loop.handle(), ipc::Server::bind(&paths::runtime_path(".sock"))?)?;
    event_loop.run(None, &mut data, |_| {}).map_err(io::Error::other)
}
