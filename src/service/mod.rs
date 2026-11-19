pub mod audio;
pub mod battery;
pub mod bluetooth;
pub mod brightness;
pub mod compositor;
pub mod dock;
pub mod hyprland;
pub mod icon;
pub mod media;
pub mod network;
pub mod palette_export;
pub mod notification;
pub mod polkit;
pub mod runtime;
pub mod settings;
pub mod shojiwm;
pub mod telemetry;
pub mod theme;
pub mod tray;
pub mod user;
pub mod wallpaper;
#[allow(dead_code)]
pub mod media_decode;

use std::cell::RefCell;
use std::io;
use std::rc::Rc;
use std::time::Duration;

use calloop::LoopHandle;
use futures_lite::future;

use crate::base::animation;
use crate::base::dbus::{self, BusKind};
use audio::AudioService;
use battery::BatteryService;
use bluetooth::BluetoothService;
use brightness::BrightnessService;
use compositor::{Compositor, make_compositor};
use media::MediaService;
use network::NetworkService;
use notification::NotificationService;
use polkit::PolkitService;
use tray::TrayService;
use settings::SettingsService;
use theme::ThemeService;

/// A bus that has not answered by then is treated as missing.
const BUS_CONNECT_TIMEOUT: Duration = Duration::from_secs(2);

/// Every backend-neutral service, owned by the app for the life of the loop.
pub struct Services {
    pub settings: Rc<SettingsService>,
    pub compositor: Rc<dyn Compositor>,
    pub system_bus: Option<zbus::Connection>,
    pub session_bus: Option<zbus::Connection>,
    pub battery: Option<Rc<BatteryService>>,
    pub bluetooth: Option<Rc<BluetoothService>>,
    pub network: Option<Rc<NetworkService>>,
    pub notifications: Rc<NotificationService>,
    pub polkit: Option<Rc<PolkitService>>,
    pub tray: Option<Rc<TrayService>>,
    pub media: Option<Rc<MediaService>>,
    pub audio: Option<Rc<AudioService>>,
    pub brightness: Rc<BrightnessService>,
    pub telemetry: Rc<RefCell<telemetry::Telemetry>>,
    pub theme: Rc<ThemeService>,
}

impl Services {
    pub fn start<D: 'static>(handle: &LoopHandle<'static, D>) -> io::Result<Services> {
        Services::start_with(handle, None)
    }

    /// `bus_address` replaces both bus addresses when given.
    pub fn start_with<D: 'static>(handle: &LoopHandle<'static, D>, bus_address: Option<&str>) -> io::Result<Services> {
        runtime::attach(handle)?;
        let settings = SettingsService::start();
        animation::set_instant(settings.config().animations_disabled);
        let weak = Rc::downgrade(&settings);
        settings.changed.connect(move |_| {
            if let Some(settings) = weak.upgrade() {
                animation::set_instant(settings.config().animations_disabled);
            }
        });

        let compositor = make_compositor();
        log_compositor(&compositor);

        let system_bus = connect_bus(BusKind::System, bus_address);
        let session_bus = connect_bus(BusKind::Session, bus_address);
        let battery = system_bus.as_ref().map(BatteryService::start);
        let network = system_bus.as_ref().map(NetworkService::start);
        let bluetooth = system_bus.as_ref().map(BluetoothService::start);
        let notifications = NotificationService::start(session_bus.as_ref());
        post_messages(&notifications, network.as_ref(), bluetooth.as_ref());
        let polkit = system_bus.as_ref().map(PolkitService::start);
        let tray = session_bus.as_ref().map(TrayService::start);
        let media = session_bus.as_ref().map(MediaService::start);
        let audio = AudioService::start();
        let brightness = BrightnessService::start();
        let telemetry = Rc::new(RefCell::new(telemetry::start()));
        let theme = ThemeService::start(&settings, compositor.clone());
        Ok(Services { settings, compositor, system_bus, session_bus, battery, bluetooth, network, notifications, polkit, tray, media, audio, brightness, telemetry, theme })
    }
}

/// Connecting is a local handshake done once before the loop runs, bounded by `BUS_CONNECT_TIMEOUT`.
fn connect_bus(kind: BusKind, address: Option<&str>) -> Option<zbus::Connection> {
    let connect = async {
        match address {
            Some(address) => zbus::connection::Builder::address(address)?.build().await,
            None => dbus::connect(kind).await,
        }
    };
    let timeout = async {
        async_io::Timer::after(BUS_CONNECT_TIMEOUT).await;
        Err(zbus::Error::Failure("timed out".into()))
    };
    let name = match kind {
        BusKind::System => "system",
        BusKind::Session => "session",
    };
    match future::block_on(future::or(connect, timeout)) {
        Ok(connection) => Some(connection),
        Err(e) => {
            ::log::info!("dbus: no {name} bus: {e}");
            None
        }
    }
}

/// Connection notices become notifications, kept for 6 s.
fn post_messages(notifications: &Rc<NotificationService>, network: Option<&Rc<NetworkService>>, bluetooth: Option<&Rc<BluetoothService>>) {
    const NOTICE_MS: i32 = 6000;
    if let Some(network) = network {
        let notifications = Rc::downgrade(notifications);
        network.messages.connect(move |m| {
            if let Some(n) = notifications.upgrade() {
                n.post("Network", &m.summary, &m.body, NOTICE_MS, m.click_command.clone());
            }
        });
    }
    if let Some(bluetooth) = bluetooth {
        let notifications = Rc::downgrade(notifications);
        bluetooth.messages.connect(move |m| {
            if let Some(n) = notifications.upgrade() {
                n.post("Bluetooth", &m.summary, &m.body, NOTICE_MS, String::new());
            }
        });
    }
}

fn log_compositor(compositor: &Rc<dyn Compositor>) {
    ::log::info!("compositor: {}", compositor.name());
    let weak = Rc::downgrade(compositor);
    compositor.active_changed().connect(move |_| {
        let Some(compositor) = weak.upgrade() else { return };
        let state = compositor.state();
        let active = state.by_monitor.get(&state.focused_monitor).map_or(-1, |m| m.active_id);
        ::log::info!("compositor: active workspace {active} on {}", state.focused_monitor);
    });
    compositor.structure_changed().connect(|_| ::log::info!("compositor: structure changed"));
}

#[cfg(test)]
mod tests {
    use super::*;
    use calloop::EventLoop;

    #[test]
    fn services_start_without_buses() {
        let event_loop: EventLoop<'static, ()> = EventLoop::try_new().unwrap();
        let services = Services::start_with(&event_loop.handle(), Some("unix:path=/nonexistent")).unwrap();
        assert!(services.system_bus.is_none() && services.session_bus.is_none());
        assert!(["Hyprland", "ShojiWM", "none"].contains(&services.compositor.name()));
    }
}

