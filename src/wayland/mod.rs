//! Wayland connection, sctk handlers, seat input and one bar per output.

pub mod frame;
pub mod input;
pub mod capture;
pub mod clipboard;
pub mod monitor;
pub mod overlays;
pub mod presentation;
pub mod render;
pub mod text_input;
pub mod toplevel;
pub mod windows;

use std::ffi::c_void;
use std::rc::Rc;
use std::time::{SystemTime, UNIX_EPOCH};

use calloop::LoopHandle;
use calloop::timer::{TimeoutAction, Timer};
use smithay_client_toolkit::compositor::{CompositorHandler, CompositorState};
use smithay_client_toolkit::output::{OutputHandler, OutputState};
use smithay_client_toolkit::reexports::calloop_wayland_source::WaylandSource;
use smithay_client_toolkit::registry::{ProvidesRegistryState, RegistryState};
use smithay_client_toolkit::shell::WaylandSurface;
use smithay_client_toolkit::seat::keyboard::{KeyEvent as SctkKeyEvent, KeyboardHandler, Keysym, Modifiers, RawModifiers};
use smithay_client_toolkit::seat::pointer::cursor_shape::CursorShapeManager;
use smithay_client_toolkit::seat::pointer::{PointerEvent, PointerEventKind, PointerHandler};
use smithay_client_toolkit::seat::{Capability, SeatHandler, SeatState};
use smithay_client_toolkit::shell::wlr_layer::{LayerShell, LayerShellHandler, LayerSurface, LayerSurfaceConfigure};
use smithay_client_toolkit::shell::xdg::XdgShell;
use smithay_client_toolkit::shell::xdg::popup::{Popup, PopupConfigure, PopupHandler};
use smithay_client_toolkit::{delegate_compositor, delegate_keyboard, delegate_layer, delegate_output, delegate_pointer, delegate_registry, delegate_seat, delegate_xdg_popup, delegate_xdg_window, registry_handlers};
use wayland_client::globals::registry_queue_init;
use wayland_client::protocol::wl_keyboard::WlKeyboard;
use wayland_client::protocol::wl_output::{Transform, WlOutput};
use wayland_client::protocol::wl_pointer::WlPointer;
use wayland_client::protocol::wl_seat::WlSeat;
use wayland_client::protocol::wl_surface::WlSurface;
use wayland_client::{Connection, Dispatch, Proxy, QueueHandle};

use crate::app::App;
use crate::app::shell::ShellVerb;
use crate::base::input::KeyEvent;
use crate::base::wall_clock::until_next_second;
use crate::config::settings::{Config, bar_disabled_for, notifications_enabled_for, osd_enabled_for};
use crate::modules::bar::model::Sources;
use crate::modules::bar::panel::content::PanelSources;
use crate::modules::bar::wayland::bar::{Bar, BarPress, Controls};
use crate::service::Services;
use crate::service::network::network_visible_count;
use monitor::{FrameTarget, Monitor, Reasons, Redraw};
use overlays::{KeyRoute, OverlayId, Overlays, route_key};
use render::toplevel_window::Request;
use windows::{WindowId, Windows};
use crate::modules::rain::wayland::Rain;
use smithay_client_toolkit::seat::pointer::BTN_LEFT;
use clipboard::Clipboard;
use text_input::{TextInput, TextInputEdit};
use smithay_client_toolkit::data_device_manager::data_device::DataDeviceHandler;
use smithay_client_toolkit::data_device_manager::data_offer::{DataOfferHandler, DragOffer};
use smithay_client_toolkit::data_device_manager::data_source::DataSourceHandler;
use smithay_client_toolkit::data_device_manager::{DataDeviceManagerState, WritePipe};
use smithay_client_toolkit::delegate_data_device;
use smithay_client_toolkit::reexports::client::protocol::wl_data_device::WlDataDevice;
use smithay_client_toolkit::reexports::client::protocol::wl_data_device_manager::DndAction;
use smithay_client_toolkit::reexports::client::protocol::wl_data_source::WlDataSource;
use smithay_client_toolkit::reexports::protocols::wp::text_input::zv3::client::zwp_text_input_manager_v3::ZwpTextInputManagerV3;
use smithay_client_toolkit::reexports::protocols::wp::text_input::zv3::client::zwp_text_input_v3::{self, ZwpTextInputV3};
use crate::modules::launcher::wayland::Launcher;
use crate::modules::logout::wayland::Logout;
use crate::modules::polkit::wayland::Polkit;
use crate::modules::overview::wayland::{CAPTURE_INTERVAL, Overview};
use crate::modules::wallpaper::wayland::{MenuPick, Wallpaper};
use crate::modules::wallpaper_select::wayland::WallpaperSelect;
use crate::modules::settings::wayland::{self as settings_host, Settings};
use crate::modules::osd::model::Kind as OsdKind;
use crate::modules::osd::wayland::{Osd, OsdShow};
use crate::service::audio::AudioKind;
use crate::modules::notification::model::{Model as NotificationModel, Press};
use crate::modules::notification::wayland::Notifications;
use crate::base::input::Button;
use input::{Pointer, button_from_code, translate_key};
use render::egl::Egl;
use render::renderer::Renderer;

const SCAN_REDRAW: std::time::Duration = std::time::Duration::from_millis(400);

pub struct State {
    pub core: App,
    pub registry: RegistryState,
    pub outputs: OutputState,
    pub compositor: CompositorState,
    pub layer_shell: LayerShell,
    pub seat: SeatState,
    pub cursor_shape: Option<CursorShapeManager>,
    pub pointer: Pointer,
    pub wl_pointer: Option<WlPointer>,
    pub keyboard: Option<WlKeyboard>,
    pub modifiers: Modifiers,
    pub handle: LoopHandle<'static, State>,
    pub egl: Egl,
    pub renderer: Renderer,
    pub qh: QueueHandle<State>,
    /// Handed to the bars so finished image decodes repaint them.
    pub redraw: Redraw,
    pub monitors: Vec<Monitor>,
    /// Popups need it; without it panels show no menu.
    pub xdg_shell: Option<XdgShell>,
    pub wl_seat: Option<WlSeat>,
    /// IPC verbs bound to the overlays.
    pub shell: crate::app::shell::Shell<State>,
    pub overlays: Overlays,
    /// The surface holding the keyboard focus, as the compositor last said.
    pub keyboard_focus: Option<WlSurface>,
    /// The output the pointer was last on; an overlay opens there when the compositor names none.
    pub pointer_output: Option<WlOutput>,
    /// The notification cards every monitor's stack shows.
    pub notification_model: NotificationModel,
    pub text_input_manager: Option<ZwpTextInputManagerV3>,
    pub text_input: TextInput,
    pub clipboard: Option<Rc<Clipboard>>,
    /// The compositor's windows as foreign toplevels, paired to its clients for capture.
    pub toplevels: toplevel::ToplevelList,
    pub capture: capture::ToplevelExportState,
    /// The output names, shared with the settings, which list them.
    pub output_names: Rc<std::cell::RefCell<Vec<String>>>,
    pub display: wayland_client::protocol::wl_display::WlDisplay,
    pub session_lock: smithay_client_toolkit::session_lock::SessionLockState,
    pub lock: crate::modules::lock::wayland::lock::Lock,
    /// The compositor confirmed the session lock; every other surface stays hidden.
    pub session_locked: bool,
    pub windows: Windows,
}

/// What a surface host needs from `State` to draw and to create child surfaces, borrowed beside the monitors.
pub struct Ctx<'a> {
    pub egl: &'a Egl,
    pub renderer: &'a mut Renderer,
    pub qh: &'a QueueHandle<State>,
    pub compositor: &'a CompositorState,
    pub xdg: Option<&'a XdgShell>,
    pub seat: Option<&'a WlSeat>,
}

impl crate::app::shell::Host for State {
    fn shell(&self) -> Option<&crate::app::shell::Shell<State>> {
        Some(&self.shell)
    }
}

impl AsMut<App> for State {
    fn as_mut(&mut self) -> &mut App {
        &mut self.core
    }
}

impl Drop for State {
    fn drop(&mut self) {
        self.text_input.destroy();
        for id in WindowId::ALL {
            self.close_window(id);
        }
        self.capture.destroy(&self.renderer);
        self.overlays.destroy(&self.egl, &self.renderer);
        for monitor in self.monitors.drain(..) {
            if let Some(n) = monitor.notifications {
                n.destroy(&self.egl, &self.renderer);
            }
            if let Some(osd) = monitor.osd {
                osd.destroy(&self.egl, &self.renderer);
            }
            if let Some(wallpaper) = monitor.wallpaper {
                wallpaper.destroy(&self.egl, &self.renderer);
            }
            if let Some(bar) = monitor.bar {
                bar.destroy(&self.egl, &self.renderer);
            }
        }
    }
}

/// The overlay a verb toggles.
fn overlay_of(verb: ShellVerb) -> Option<OverlayId> {
    match verb {
        ShellVerb::Logout => Some(OverlayId::Logout),
        ShellVerb::Launcher | ShellVerb::LauncherGlobal => Some(OverlayId::Launcher),
        ShellVerb::Overview => Some(OverlayId::Overview),
        ShellVerb::Settings => Some(OverlayId::Settings),
        ShellVerb::Wallpaper => Some(OverlayId::WallpaperSelect),
        _ => None,
    }
}

/// Every service signal that changes what a bar shows asks for one coalesced repaint.
fn wire_services(services: &Services, redraw: &Redraw) {
    let all = |redraw: &Redraw| {
        let redraw = redraw.clone();
        move |_: &_| redraw.all()
    };
    let config = redraw.clone();
    services.settings.changed.connect(move |_| config.config());
    services.theme.changed.connect(all(redraw));
    services.compositor.active_changed().connect(all(redraw));
    services.compositor.structure_changed().connect(all(redraw));
    let notifications = redraw.clone();
    services.notifications.changed.connect(move |_| notifications.notifications());
    services.brightness.changed.connect(all(redraw));
    let (osd, brightness) = (redraw.clone(), services.brightness.clone());
    services.brightness.changed.connect(move |_| osd.osd(OsdShow { kind: OsdKind::Brightness, percent: brightness.percent(), muted: false }));
    if let Some(audio) = &services.audio {
        let redraw = redraw.clone();
        let weak = Rc::downgrade(audio);
        audio.changed.connect(move |kind| {
            redraw.all();
            let Some(audio) = weak.upgrade() else { return };
            let (osd_kind, level) = match kind {
                AudioKind::Sink => (OsdKind::Volume, audio.sink()),
                AudioKind::Source => (OsdKind::Mic, audio.source()),
                AudioKind::Nodes => return,
            };
            redraw.osd(OsdShow { kind: osd_kind, percent: if level.present { level.percent } else { 0 }, muted: level.muted });
        });
    }
    if let Some(battery) = &services.battery {
        battery.changed.connect(all(redraw));
    }
    if let Some(network) = &services.network {
        network.changed.connect(all(redraw));
    }
    if let Some(bluetooth) = &services.bluetooth {
        bluetooth.changed.connect(all(redraw));
    }
    if let Some(tray) = &services.tray {
        tray.changed.connect(all(redraw));
    }
    if let Some(polkit) = &services.polkit {
        let redraw = redraw.clone();
        polkit.changed.connect(move |_| redraw.polkit());
    }
}

/// Connects, binds the globals, brings up EGL and the renderer, and creates a bar on every output already present.
pub fn connect(core: App, handle: &LoopHandle<'static, State>) -> Result<State, String> {
    let conn = Connection::connect_to_env().map_err(|e| e.to_string())?;
    let display = conn.display();
    let (globals, mut queue) = registry_queue_init::<State>(&conn).map_err(|e| e.to_string())?;
    let qh = queue.handle();
    let compositor = CompositorState::bind(&globals, &qh).map_err(|_| "no wl_compositor")?;
    let layer_shell = LayerShell::bind(&globals, &qh).map_err(|_| "no zwlr_layer_shell_v1")?;
    presentation::bind(&globals, &qh);
    let egl = Egl::from_wayland(conn.backend().display_ptr() as *mut c_void)?;
    if !egl.make_current(None) {
        return Err("egl: cannot make the context current".into());
    }
    let renderer = Renderer::new(egl.gl())?;
    let redraw = Redraw::new(handle, |reasons, state: &mut State| state.repaint(reasons)).map_err(|e| e.to_string())?;
    if let Some(services) = &core.services {
        wire_services(services, &redraw);
    }
    let mut shell = crate::app::shell::Shell::new();
    shell.track("bar", |state: &State| state.monitors.iter().any(|m| m.bar.as_ref().is_some_and(|b| b.panels.is_open())));
    shell.track("logout", |state: &State| state.overlays.logout.as_ref().is_some_and(Logout::is_open));
    shell.bind(ShellVerb::Logout, |state: &mut State| state.toggle_logout(false));
    shell.track("launcher", |state: &State| state.overlays.launcher.as_ref().is_some_and(Launcher::is_open));
    shell.bind(ShellVerb::Launcher, |state: &mut State| state.toggle_launcher(false));
    shell.bind(ShellVerb::LauncherGlobal, |state: &mut State| state.toggle_launcher(true));
    shell.track("overview", |state: &State| state.overlays.overview.as_ref().is_some_and(Overview::is_open));
    shell.bind(ShellVerb::Overview, |state: &mut State| state.toggle_overview(false));
    shell.track("wallpaper-select", |state: &State| state.overlays.wallpaper_select.as_ref().is_some_and(WallpaperSelect::is_open));
    shell.bind(ShellVerb::Wallpaper, |state: &mut State| state.toggle_wallpaper_select());
    shell.track("settings", |state: &State| state.overlays.settings.as_ref().is_some_and(Settings::is_open));
    shell.bind(ShellVerb::Settings, |state: &mut State| state.toggle_settings());
    shell.track("polkit", |state: &State| state.overlays.polkit.as_ref().is_some_and(Polkit::is_open));
    shell.track("lock", |state: &State| state.lock.is_active());
    shell.bind(ShellVerb::Lock, |state: &mut State| {
        state.lock_poll_telemetry();
        state.lock_begin(true);
    });
    shell.track("rain", |state: &State| state.windows.is_open(WindowId::Rain));
    shell.bind(ShellVerb::Rain, |state: &mut State| state.window_request(WindowId::Rain, Request::Toggle));
    shell.track("bamboo", |state: &State| state.windows.is_open(WindowId::Bamboo));
    shell.bind(ShellVerb::Bamboo, |state: &mut State| state.window_request(WindowId::Bamboo, Request::Toggle));
    shell.track("visualizer", |state: &State| state.windows.is_open(WindowId::Visualizer));
    shell.bind(ShellVerb::Visualizer, |state: &mut State| state.window_request(WindowId::Visualizer, Request::Toggle));
    shell.after_verb(|state: &mut State, verb| {
        state.close_overlays_except(overlay_of(verb));
        if verb == ShellVerb::Lock {
            let (monitors, mut ctx) = state.split();
            for bar in monitors.iter_mut().filter_map(|m| m.bar.as_mut()) {
                bar.panels.close_all(&mut ctx);
            }
        } else {
            state.close_panels_under_overlay();
        }
    });
    let mut state = State {
        core,
        registry: RegistryState::new(&globals),
        outputs: OutputState::new(&globals, &qh),
        compositor,
        layer_shell,
        seat: SeatState::new(&globals, &qh),
        cursor_shape: CursorShapeManager::bind(&globals, &qh).ok(),
        pointer: Pointer::default(),
        wl_pointer: None,
        keyboard: None,
        modifiers: Modifiers::default(),
        handle: handle.clone(),
        egl,
        renderer,
        qh: qh.clone(),
        redraw,
        monitors: Vec::new(),
        xdg_shell: XdgShell::bind(&globals, &qh).map_err(|e| ::log::warn!("panel: no xdg_wm_base, no popups: {e}")).ok(),
        wl_seat: None,
        shell,
        overlays: Overlays { logout: Some(Logout::new()), ..Overlays::default() },
        keyboard_focus: None,
        pointer_output: None,
        notification_model: NotificationModel::default(),
        text_input_manager: globals.bind(&qh, 1..=1, ()).map_err(|_| ::log::info!("text-input: unavailable")).ok(),
        text_input: TextInput::default(),
        clipboard: DataDeviceManagerState::bind(&globals, &qh)
            .map_err(|e| ::log::info!("clipboard: no wl_data_device_manager: {e}"))
            .ok()
            .map(|manager| Rc::new(Clipboard::new(manager, qh.clone(), handle.clone()))),
        toplevels: toplevel::ToplevelList::bind(&globals, &qh),
        capture: capture::ToplevelExportState::bind(&globals, &qh),
        output_names: Rc::default(),
        display,
        session_lock: smithay_client_toolkit::session_lock::SessionLockState::new(&globals, &qh),
        lock: crate::modules::lock::wayland::lock::Lock::new({
            let handle = handle.clone();
            Rc::new(move || {
                handle.insert_idle(|state: &mut State| state.lock_request_all());
            })
        }),
        session_locked: false,
        windows: Windows::default(),
    };
    state.windows.rain = Some(Rain::new(&state.config().rain));
    let wake = state.redraw.clone();
    let clipboard = state.clipboard.clone().map(|c| c as Rc<dyn crate::base::input::Clipboard>);
    state.overlays.launcher = Some(Launcher::new(Rc::new(move || wake.launcher()), clipboard));
    if let Some(services) = &state.core.services {
        let notifications = services.notifications.clone();
        let notify = Box::new(move |summary: &str, body: &str| {
            notifications.post("astralia-shell", summary, body, -1, String::new());
        });
        state.overlays.overview = Some(Overview::new(services.compositor.clone(), notify));
        let wake = state.redraw.clone();
        state.overlays.wallpaper_select = Some(WallpaperSelect::new(services.settings.clone(), Rc::new(move || wake.wallpaper_select())));
        let compositor = services.compositor.clone();
        let hooks = settings_host::hooks(services.settings.clone(), state.output_names.clone(), Box::new(move || compositor.state().focused_monitor.clone()));
        let wake = state.redraw.clone();
        let clipboard = state.clipboard.clone().map(|c| c as Rc<dyn crate::base::input::Clipboard>);
        state.overlays.settings = Some(Settings::new(hooks, Rc::new(move || wake.settings()), clipboard));
        if services.polkit.is_some() {
            let wake = state.redraw.clone();
            state.overlays.polkit = Some(Polkit::new(Rc::new(move || wake.polkit())));
        }
    }
    queue.roundtrip(&mut state).map_err(|e| e.to_string())?;
    if crate::app::session::start_locked_pending() {
        state.lock_at_startup();
    }
    WaylandSource::new(conn, queue).insert(handle.clone()).map_err(|e| e.error.to_string())?;
    let to_next_second = || until_next_second(SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().subsec_nanos());
    handle
        .insert_source(Timer::from_duration(to_next_second()), move |_, _, state: &mut State| {
            state.repaint(Reasons { all: true, ..Reasons::default() });
            state.lock_timer_tick();
            if state.overlays.launcher.as_mut().is_some_and(Launcher::blink) {
                state.request_overlay_frame(OverlayId::Launcher);
            }
            if state.overlays.settings.as_mut().is_some_and(Settings::blink) {
                state.request_overlay_frame(OverlayId::Settings);
            }
            TimeoutAction::ToDuration(to_next_second())
        })
        .map_err(|e| e.error.to_string())?;
    // While a scan has found nothing yet, the bar and the network panel follow it closely.
    handle
        .insert_source(Timer::from_duration(SCAN_REDRAW), |_, _, state: &mut State| {
            let scanning = state.core.services.as_ref().and_then(|s| s.network.as_ref()).is_some_and(|n| n.scanning() && n.wifi_enabled() && network_visible_count(&n.networks()) == 0);
            if scanning {
                state.repaint(Reasons { all: true, panels: true, ..Reasons::default() });
            }
            TimeoutAction::ToDuration(SCAN_REDRAW)
        })
        .map_err(|e| e.error.to_string())?;
    Ok(state)
}

impl State {
    /// The monitor whose surfaces include `surface`.
    pub fn monitor_for_surface(&self, surface: &WlSurface) -> Option<usize> {
        self.monitors.iter().position(|m| {
            m.bar.as_ref().is_some_and(|b| b.window.owns(surface) || b.panels.owns(surface))
                || m.wallpaper.as_ref().is_some_and(|w| w.owns(surface))
                || m.osd.as_ref().is_some_and(|o| o.owns(surface))
                || m.notifications.as_ref().is_some_and(|n| n.owns(surface))
        })
    }

    /// The monitors and a `Ctx` over everything else.
    fn split(&mut self) -> (&mut [Monitor], Ctx<'_>) {
        let ctx = Ctx { egl: &self.egl, renderer: &mut self.renderer, qh: &self.qh, compositor: &self.compositor, xdg: self.xdg_shell.as_ref(), seat: self.wl_seat.as_ref() };
        (&mut self.monitors, ctx)
    }

    /// The live config, or the defaults before the services exist.
    pub(crate) fn config(&self) -> Config {
        self.core.services.as_ref().map(|s| s.settings.config().clone()).unwrap_or_default()
    }

    fn draw(&mut self, index: usize) {
        let cfg = self.config();
        let name = &self.monitors[index].name;
        let overview_here = self.overlays.overview.as_ref().is_some_and(|o| o.shown_on(name));
        let logout_here = self.overlays.logout.as_ref().is_some_and(|l| l.shown_on(name));
        let Some(bar) = &mut self.monitors[index].bar else { return };
        let (open, clock_open) = bar.panels.open_item();
        use crate::modules::bar::model::Item;
        let open = open.or(logout_here.then_some(Item::Logout)).or(overview_here.then_some(Item::WorkspaceInfo));
        if bar.draw(&self.egl, &mut self.renderer, &cfg, open, clock_open || overview_here) {
            bar.window.request_frame(&self.qh);
        }
    }

    fn request_panel_frame(&mut self, index: usize) {
        let (monitors, mut ctx) = self.split();
        if let Some(bar) = &mut monitors[index].bar {
            bar.panels.request_frame(&mut ctx);
        }
    }

    fn draw_wallpaper(&mut self, index: usize) {
        let Some(wallpaper) = &mut self.monitors[index].wallpaper else { return };
        if wallpaper.draw(&self.egl, &mut self.renderer) {
            wallpaper.window.request_frame(&self.qh);
        }
    }

    pub(crate) fn request_wallpaper_frame(&mut self, index: usize) {
        if self.monitors[index].wallpaper.as_mut().is_some_and(|w| w.window.request_frame(&self.qh)) {
            self.draw_wallpaper(index);
        }
    }

    /// Pointer events on a wallpaper surface: the menu hover and clicks, and what a menu click asks for.
    fn wallpaper_pointer(&mut self, i: usize, event: &PointerEvent) {
        let Some(wallpaper) = &mut self.monitors[i].wallpaper else { return };
        let (x, y) = (event.position.0 as f32, event.position.1 as f32);
        let (repaint, pick) = match event.kind {
            PointerEventKind::Enter { .. } | PointerEventKind::Motion { .. } => (wallpaper.pointer(Some((x, y))), None),
            PointerEventKind::Leave { .. } => (wallpaper.pointer(None), None),
            PointerEventKind::Press { button, .. } => wallpaper.press(button_from_code(button), x, y),
            _ => (false, None),
        };
        self.pointer.set_cursor(wallpaper.wants_hand());
        if repaint {
            self.request_wallpaper_frame(i);
        }
        match pick {
            Some(MenuPick::Settings) => {
                crate::app::shell::Shell::run(self, crate::app::shell::ShellVerb::Settings);
            }
            Some(MenuPick::Wallpaper { column }) => {
                self.close_overlays_except(Some(OverlayId::WallpaperSelect));
                self.open_wallpaper_select(i, column as i32);
                self.close_panels_under_overlay();
            }
            None => {}
        }
    }

    /// Creates or removes the notification surface as the config enables it on this output.
    /// Creates or destroys monitor `i`'s bar, with its panels, to follow "disable bar".
    fn sync_bar_surface(&mut self, i: usize, cfg: &Config) {
        let name = self.monitors[i].name.clone();
        match (!bar_disabled_for(cfg, &name), self.monitors[i].bar.is_some()) {
            (true, false) => {
                let Some((sources, controls, panels)) = self.bar_sources(&name) else { return };
                let monitor = &self.monitors[i];
                let mut bar = Bar::new(&self.compositor, &self.layer_shell, &self.qh, &monitor.output, &name, cfg, sources, controls, &panels, &self.redraw);
                let redraw = self.redraw.clone();
                bar.set_on_image_ready(Rc::new(move || redraw.all()));
                bar.window.set_scale(monitor.scale);
                ::log::info!("bar: created on '{name}'");
                self.monitors[i].bar = Some(bar);
            }
            (false, true) => {
                if let Some(bar) = self.monitors[i].bar.take() {
                    self.forget_surfaces(&bar.surface_ids());
                    bar.destroy(&self.egl, &self.renderer);
                    ::log::info!("bar: disabled on '{name}'");
                }
            }
            _ => {}
        }
    }

    fn sync_notification_surface(&mut self, i: usize, cfg: &Config) {
        let want = notifications_enabled_for(cfg, &self.monitors[i].name);
        match (want, self.monitors[i].notifications.is_some()) {
            (true, false) => {
                let monitor = &self.monitors[i];
                let mut surface = Notifications::new(&self.compositor, &self.layer_shell, &self.qh, &monitor.output);
                surface.window.set_scale(monitor.scale);
                self.monitors[i].notifications = Some(surface);
            }
            (false, true) => {
                if let Some(surface) = self.monitors[i].notifications.take() {
                    self.forget_surfaces(&[surface.surface_id()]);
                    surface.destroy(&self.egl, &self.renderer);
                }
            }
            _ => {}
        }
    }

    fn draw_notifications(&mut self, index: usize) {
        let Some(surface) = &mut self.monitors[index].notifications else { return };
        if surface.draw(&self.egl, &mut self.renderer, &self.compositor, &mut self.notification_model) {
            surface.window.request_frame(&self.qh);
        }
    }

    fn request_notification_frame(&mut self, index: usize) {
        if self.monitors[index].notifications.as_mut().is_some_and(|n| n.window.request_frame(&self.qh)) {
            self.draw_notifications(index);
        }
    }

    /// Pointer events on a notification stack: hover, the close buttons and clickable cards.
    fn notification_pointer(&mut self, i: usize, event: &PointerEvent) {
        let Some(surface) = &mut self.monitors[i].notifications else { return };
        let (x, y) = event.position;
        let repaint = match event.kind {
            PointerEventKind::Enter { .. } | PointerEventKind::Motion { .. } => surface.pointer(Some((x, y))),
            PointerEventKind::Leave { .. } => surface.pointer(None),
            PointerEventKind::Press { button, .. } if button_from_code(button) == Button::Left => match surface.press(x, y) {
                Press::Closed => true,
                Press::Open(id) => {
                    if let Some(services) = &self.core.services {
                        services.notifications.click(id);
                    }
                    false
                }
                Press::Nothing => false,
            },
            _ => false,
        };
        if let Some(surface) = &self.monitors[i].notifications {
            self.pointer.set_cursor(surface.wants_hand());
        }
        if repaint {
            self.request_notification_frame(i);
        }
    }

    fn draw_osd(&mut self, index: usize) {
        let Some(osd) = &mut self.monitors[index].osd else { return };
        if osd.draw(&self.egl, &mut self.renderer) {
            osd.window.request_frame(&self.qh);
        }
    }

    fn request_osd_frame(&mut self, index: usize) {
        self.request_osd_frame_only(index);
        let Some(osd) = &self.monitors[index].osd else { return };
        // The card hides itself at its deadline; a wakeup left over from an earlier show finds nothing due.
        let wake = osd.until_hide(std::time::Instant::now()) + std::time::Duration::from_millis(10);
        let armed = self.handle.insert_source(Timer::from_duration(wake), |_, _, state: &mut State| {
            let now = std::time::Instant::now();
            for i in 0..state.monitors.len() {
                if state.monitors[i].osd.as_mut().is_some_and(|o| o.tick_hide(now)) {
                    state.request_osd_frame_only(i);
                }
            }
            TimeoutAction::Drop
        });
        if let Err(e) = armed {
            ::log::error!("osd: cannot arm the hide timer: {e}");
        }
    }

    fn request_osd_frame_only(&mut self, index: usize) {
        if self.monitors[index].osd.as_mut().is_some_and(|o| o.window.request_frame(&self.qh)) {
            self.draw_osd(index);
        }
    }

    fn request_frame(&mut self, index: usize) {
        if self.monitors[index].bar.as_mut().is_some_and(|b| b.window.request_frame(&self.qh)) {
            self.draw(index);
        }
    }

    /// Re-applies a changed config, follows the compositor's gaps, and requests a frame on every bar.
    fn repaint(&mut self, reasons: Reasons) {
        let cfg = self.config();
        if reasons.launcher
            && let Some(launcher) = &mut self.overlays.launcher
        {
            launcher.poll();
            self.request_overlay_frame(OverlayId::Launcher);
        }
        if reasons.wallpaper_select
            && let Some(picker) = &mut self.overlays.wallpaper_select
        {
            picker.poll();
            self.request_overlay_frame(OverlayId::WallpaperSelect);
        }
        if (reasons.settings || reasons.config)
            && let Some(settings) = &mut self.overlays.settings
        {
            settings.poll();
            self.request_overlay_frame(OverlayId::Settings);
        }
        if reasons.polkit {
            self.sync_polkit();
        }
        if reasons.config || reasons.all {
            self.windows_apply_config(&cfg);
        }
        if reasons.notifications
            && let Some(services) = &self.core.services
        {
            self.notification_model.sync(&services.notifications.list());
        }
        for i in 0..self.monitors.len() {
            if reasons.config {
                self.sync_bar_surface(i, &cfg);
                if let Some(bar) = &mut self.monitors[i].bar {
                    bar.apply_config(&cfg);
                }
                if let Some(wallpaper) = &mut self.monitors[i].wallpaper {
                    wallpaper.sync_from_config(&self.renderer, &cfg);
                }
            }
            // A finished decode repaints only the surface showing that wallpaper.
            let pending = reasons.wallpaper && self.monitors[i].wallpaper.as_ref().is_some_and(Wallpaper::has_pending);
            match monitor::wallpaper_frame_target(self.lock.is_active(), reasons.config, pending) {
                FrameTarget::Background => self.request_wallpaper_frame(i),
                // A custom lock wallpaper hides the desktop one; nothing to repaint.
                FrameTarget::Lock if self.lock.wallpaper.loaded() => {}
                FrameTarget::Lock => {
                    // The covered wallpaper surface may get no frame callbacks, so upload here.
                    if self.egl.make_current(None)
                        && let Some(wallpaper) = &mut self.monitors[i].wallpaper
                    {
                        wallpaper.upload(&self.renderer);
                    }
                    let name = self.monitors[i].name.clone();
                    self.lock_request_output(&name);
                }
                FrameTarget::None => {}
            }
            if reasons.config {
                self.sync_notification_surface(i, &cfg);
            }
            if reasons.notifications {
                self.request_notification_frame(i);
            }
            if let Some(show) = reasons.osd
                && osd_enabled_for(&cfg, &self.monitors[i].name)
                && self.monitors[i].osd.as_mut().is_some_and(|o| o.show(show))
            {
                self.request_osd_frame(i);
            }
            if reasons.all || reasons.config {
                if let Some(bar) = &mut self.monitors[i].bar {
                    bar.tick(&cfg);
                }
                self.request_frame(i);
            }
            if reasons.panels {
                self.request_panel_frame(i);
            }
        }
    }

    fn bar_sources(&self, output: &str) -> Option<(Sources, Controls, PanelSources)> {
        let s = self.core.services.as_ref()?;
        let tray_occupied = s.tray.clone().map(|tray| Rc::new(move || !tray.items().is_empty()) as Rc<dyn Fn() -> bool>);
        let sources = Sources {
            audio: s.audio.clone(),
            battery: s.battery.clone(),
            bluetooth: s.bluetooth.clone(),
            network: s.network.clone(),
            brightness: Some(s.brightness.clone()),
            compositor: s.compositor.clone(),
            tray_occupied,
            output: output.to_string(),
            resource: true,
            workspace_slots: 0,
        };
        let controls = Controls { compositor: s.compositor.clone(), audio: s.audio.clone(), brightness: Some(s.brightness.clone()) };
        let panels = PanelSources {
            tray: s.tray.clone(),
            telemetry: Some(s.telemetry.clone()),
            network: s.network.clone(),
            bluetooth: s.bluetooth.clone(),
            audio: s.audio.clone(),
            media: s.media.clone(),
            battery: s.battery.clone(), brightness: Some(s.brightness.clone()), output: output.to_string() };
        Some((sources, controls, panels))
    }

    /// A translated key press or repeat: to the overlay holding the keyboard, else to the first open panel.
    fn key(&mut self, event: KeyEvent) {
        let open: Vec<bool> = self.monitors.iter().map(|m| m.bar.as_ref().is_some_and(|b| b.panels.is_open())).collect();
        match route_key(self.lock.is_active(), self.keyboard_focus.as_ref(), |s| self.windows.owner_of(s), |s| self.overlays.owner_of(s), &open) {
            KeyRoute::Lock => self.lock_handle_key(&event),
            KeyRoute::Window(id) => self.window_request(id, Request::Key(&event)),
            KeyRoute::Overlay(OverlayId::Launcher) => {
                if let Some(launcher) = &mut self.overlays.launcher {
                    launcher.key(&self.compositor, &event);
                }
                self.sync_launcher_text_input();
                self.request_overlay_frame(OverlayId::Launcher);
            }
            KeyRoute::Overlay(OverlayId::Logout) => {
                if let Some(logout) = &mut self.overlays.logout {
                    logout.key(&self.compositor, &event);
                }
                self.request_overlay_frame(OverlayId::Logout);
            }
            KeyRoute::Overlay(OverlayId::Overview) => {
                if let Some(overview) = &mut self.overlays.overview {
                    overview.key(&self.compositor, &event);
                }
                self.request_overlay_frame(OverlayId::Overview);
            }
            KeyRoute::Overlay(OverlayId::WallpaperSelect) => {
                if let Some(picker) = &mut self.overlays.wallpaper_select {
                    picker.key(&self.compositor, &event);
                }
                self.request_overlay_frame(OverlayId::WallpaperSelect);
            }
            KeyRoute::Overlay(OverlayId::Polkit) => {
                let service = self.core.services.as_ref().and_then(|s| s.polkit.as_deref());
                if let (Some(card), Some(service)) = (&mut self.overlays.polkit, service) {
                    card.key(service, &event);
                }
                self.request_overlay_frame(OverlayId::Polkit);
            }
            KeyRoute::Overlay(OverlayId::Settings) => {
                if let Some(settings) = &mut self.overlays.settings {
                    settings.key(&self.compositor, &event);
                }
                self.sync_settings_text_input();
                self.request_overlay_frame(OverlayId::Settings);
            }
            KeyRoute::Monitor(i) => {
                let (monitors, mut ctx) = self.split();
                if let Some(bar) = &mut monitors[i].bar {
                    bar.panels.key(&mut ctx, &event);
                }
            }
            KeyRoute::Nowhere => {}
        }
    }

    /// Pointer events on a panel or popup surface; returns the hand-cursor wish, `None` for events that change nothing.
    /// The panel repaints itself through its change hook.
    fn panel_pointer(&mut self, i: usize, event: &PointerEvent) -> Option<bool> {
        let (x, y) = event.position;
        let surface = &event.surface;
        let (monitors, mut ctx) = self.split();
        let Some(bar) = &mut monitors[i].bar else { return None };
        let panels = &mut bar.panels;
        match event.kind {
            PointerEventKind::Enter { .. } | PointerEventKind::Motion { .. } => panels.move_to(surface, x, y),
            PointerEventKind::Press { button, serial, .. } => panels.press(&mut ctx, surface, button_from_code(button), x, y, serial),
            PointerEventKind::Release { .. } => panels.release(),
            PointerEventKind::Axis { vertical, .. } if vertical.absolute != 0.0 => panels.wheel(surface, x, y, vertical.absolute),
            _ => return None,
        }
        Some(panels.wants_hand(surface, x, y))
    }

    /// Pointer events on a bar surface: `None` for events that change nothing, else whether the bar repaints
    /// with the hand-cursor wish (none after a leave).
    fn bar_pointer(&mut self, i: usize, event: &PointerEvent) -> Option<Option<bool>> {
        let (x, y) = event.position;
        let (monitors, mut ctx) = self.split();
        let Some(bar) = &mut monitors[i].bar else { return None };
        match event.kind {
            PointerEventKind::Enter { .. } | PointerEventKind::Motion { .. } => bar.set_pointer(Some((x, y))),
            PointerEventKind::Leave { .. } => {
                bar.set_pointer(None);
                return Some(None);
            }
            PointerEventKind::Press { button, .. } if button_from_code(button) == Button::Left => {
                bar.set_pointer(Some((x, y)));
                match bar.press(x, y) {
                    BarPress::Panel(item) => bar.toggle_panel(&mut ctx, item),
                    BarPress::Logout => {
                        self.toggle_logout(true);
                        self.close_panels_under_overlay();
                        return None;
                    }
                    BarPress::Overview => {
                        self.toggle_overview(true);
                        self.close_panels_under_overlay();
                        return None;
                    }
                    BarPress::Nothing => {}
                }
            }
            PointerEventKind::Release { .. } => {
                bar.panels.release();
                return None;
            }
            PointerEventKind::Axis { vertical, .. } if vertical.absolute != 0.0 => bar.wheel(vertical.absolute),
            _ => return None,
        }
        Some(Some(bar.wants_hand()))
    }

    /// Opens the logout overlay on the target monitor, or plays its exit.
    fn toggle_logout(&mut self, by_widget: bool) {
        let target = self.overlay_target();
        let logo = self.config().logout_logo_path;
        let Some(logout) = &mut self.overlays.logout else { return };
        let mut output = String::new();
        if !logout.is_open() {
            let Some(m) = target.map(|t| &self.monitors[t]) else { return };
            logout.overlay.bind(&self.egl, &self.compositor, &self.layer_shell, &self.qh, &m.output, &m.name);
            logout.overlay.set_scale(m.scale);
            output.clone_from(&m.name);
        }
        logout.toggle(&self.compositor, &logo, &output, by_widget);
        self.request_overlay_frame(OverlayId::Logout);
        self.redraw.all();
    }

    /// Opens the overview on the target monitor, or plays its close; thumbnails refresh while it is open.
    fn toggle_overview(&mut self, by_widget: bool) {
        let target = self.overlay_target();
        let Some(overview) = &mut self.overlays.overview else { return };
        let opening = !overview.is_open();
        let Some(m) = target.map(|t| &self.monitors[t]) else { return };
        if opening {
            overview.overlay.bind(&self.egl, &self.compositor, &self.layer_shell, &self.qh, &m.output, &m.name);
            overview.overlay.set_scale(m.scale);
        }
        overview.toggle(&self.compositor, &m.name, by_widget);
        if opening && overview.is_open() {
            let armed = self.handle.insert_source(Timer::from_duration(CAPTURE_INTERVAL), |_, _, state: &mut State| {
                if !state.overlays.overview.as_ref().is_some_and(Overview::is_open) {
                    return TimeoutAction::Drop;
                }
                state.request_overlay_frame(OverlayId::Overview);
                TimeoutAction::ToDuration(CAPTURE_INTERVAL)
            });
            if let Err(e) = armed {
                ::log::warn!("overview: no capture timer: {}", e.error);
            }
        }
        self.request_overlay_frame(OverlayId::Overview);
    }

    /// Opens the picker on the target monitor, on the column under the pointer, or fades it out.
    fn toggle_wallpaper_select(&mut self) {
        let Some(picker) = &mut self.overlays.wallpaper_select else { return };
        if picker.is_open() {
            picker.close(&self.compositor);
            self.request_overlay_frame(OverlayId::WallpaperSelect);
            return;
        }
        let Some(target) = self.overlay_target() else { return };
        let column = self.pointer_column(target);
        self.open_wallpaper_select(target, column);
    }

    /// The wallpaper column of monitor `index` under the compositor's cursor; 0 when unknown.
    fn pointer_column(&self, index: usize) -> i32 {
        let (Some(services), Some(picker)) = (&self.core.services, &self.overlays.wallpaper_select) else { return 0 };
        let name = &self.monitors[index].name;
        let state = services.compositor.state();
        let Some(monitor) = crate::service::compositor::compositor_monitor_by_name(&state, name) else { return 0 };
        let Some(cursor) = services.compositor.cursor_position() else { return 0 };
        let [width, height] = crate::service::compositor::compositor_logical_size(monitor);
        picker.column_at(name, cursor.x - monitor.x, cursor.y - monitor.y, width, height)
    }

    /// Opens the picker on `column` of monitor `index`, unless it is open already.
    fn open_wallpaper_select(&mut self, index: usize, column: i32) {
        let Some(picker) = &mut self.overlays.wallpaper_select else { return };
        if picker.is_open() {
            return;
        }
        let m = &self.monitors[index];
        picker.overlay.bind(&self.egl, &self.compositor, &self.layer_shell, &self.qh, &m.output, &m.name);
        picker.overlay.set_scale(m.scale);
        picker.open(&self.compositor, &m.name, column);
        self.request_overlay_frame(OverlayId::WallpaperSelect);
    }

    /// Opens the launcher on the target monitor, or fades it out.
    fn toggle_launcher(&mut self, global: bool) {
        let target = self.overlay_target();
        let lightning = !self.config().animations_disabled;
        let Some(launcher) = &mut self.overlays.launcher else { return };
        if !launcher.is_open() {
            let Some(m) = target.map(|t| &self.monitors[t]) else { return };
            launcher.overlay.bind(&self.egl, &self.compositor, &self.layer_shell, &self.qh, &m.output, &m.name);
            launcher.overlay.set_scale(m.scale);
        }
        launcher.toggle(&self.compositor, global, lightning);
        self.sync_launcher_text_input();
        self.request_overlay_frame(OverlayId::Launcher);
    }

    /// Gives the launcher field the input method while it is open, and takes it back as it closes.
    fn sync_launcher_text_input(&mut self) {
        let Some(launcher) = &mut self.overlays.launcher else { return };
        match launcher.text_input_change() {
            Some((true, surface)) => self.text_input.set_focused(&surface, launcher.text_input_state()),
            Some((false, surface)) => {
                self.text_input.clear_focused(&surface);
            }
            None => {}
        }
    }

    /// Opens the settings on the target monitor, or plays their close.
    fn toggle_settings(&mut self) {
        let target = self.overlay_target();
        let Some(settings) = &mut self.overlays.settings else { return };
        if !settings.is_open() {
            let Some(m) = target.map(|t| &self.monitors[t]) else { return };
            settings.overlay.bind(&self.egl, &self.compositor, &self.layer_shell, &self.qh, &m.output, &m.name);
            settings.overlay.set_scale(m.scale);
        }
        settings.toggle(&self.compositor);
        self.sync_settings_text_input();
        self.request_overlay_frame(OverlayId::Settings);
    }

    /// Gives a focused settings field the input method, and takes it back as the field or the card closes.
    fn sync_settings_text_input(&mut self) {
        let Some(settings) = &mut self.overlays.settings else { return };
        match settings.text_input_change() {
            Some((true, surface)) => self.text_input.set_focused(&surface, settings.text_input_state()),
            Some((false, surface)) => {
                self.text_input.clear_focused(&surface);
            }
            None => {}
        }
    }

    /// Follows the launcher's input-method focus and, once it has closed, destroys its surface.
    fn settle_launcher(&mut self) {
        self.sync_launcher_text_input();
        let Some(launcher) = &mut self.overlays.launcher else { return };
        if !launcher.take_surface_due() {
            return;
        }
        let gone = launcher.overlay.surface_id();
        launcher.overlay.destroy_surface(&self.egl);
        self.forget_surfaces(gone.as_slice());
    }

    /// Opens the polkit card on the target monitor while a request is pending, and closes it after.
    fn sync_polkit(&mut self) {
        let pending = self.core.services.as_ref().and_then(|s| s.polkit.as_ref()).is_some_and(|p| p.pending());
        let target = self.overlay_target();
        let Some(card) = &mut self.overlays.polkit else { return };
        if pending
            && !card.overlay.is_open_or_pending()
            && let Some(m) = target.map(|t| &self.monitors[t])
        {
            card.overlay.bind(&self.egl, &self.compositor, &self.layer_shell, &self.qh, &m.output, &m.name);
            card.overlay.set_scale(m.scale);
        }
        card.sync(&self.compositor, pending);
        self.request_overlay_frame(OverlayId::Polkit);
    }

    /// While an app-wide overlay is open, no bar panel stays open beneath it.
    fn close_panels_under_overlay(&mut self) {
        if !self.overlays.all().iter().any(|(_, o)| o.is_open_or_pending()) {
            return;
        }
        let (monitors, mut ctx) = self.split();
        for bar in monitors.iter_mut().filter_map(|m| m.bar.as_mut()) {
            bar.panels.close_all(&mut ctx);
        }
    }

    /// Closes every open overlay but `keep`, so the keyboard never stays on a hidden surface.
    fn close_overlays_except(&mut self, keep: Option<OverlayId>) {
        if overlays::closed_by_verb(OverlayId::Launcher, keep)
            && let Some(launcher) = self.overlays.launcher.as_mut().filter(|l| l.is_open())
        {
            launcher.fast_close(&self.compositor);
            self.settle_launcher();
        }
        if overlays::closed_by_verb(OverlayId::Logout, keep)
            && let Some(logout) = self.overlays.logout.as_mut().filter(|l| l.is_open())
        {
            logout.fast_close(&self.compositor);
            self.request_overlay_frame(OverlayId::Logout);
        }
        if overlays::closed_by_verb(OverlayId::Overview, keep)
            && let Some(overview) = self.overlays.overview.as_mut().filter(|o| o.is_open())
        {
            overview.fast_close(&self.compositor);
            self.request_overlay_frame(OverlayId::Overview);
        }
        if overlays::closed_by_verb(OverlayId::WallpaperSelect, keep)
            && let Some(picker) = self.overlays.wallpaper_select.as_mut().filter(|p| p.is_open())
        {
            picker.fast_close(&self.compositor);
            self.request_overlay_frame(OverlayId::WallpaperSelect);
        }
        if overlays::closed_by_verb(OverlayId::Settings, keep)
            && let Some(settings) = self.overlays.settings.as_mut().filter(|s| s.is_open())
        {
            settings.fast_close(&self.compositor);
            self.sync_settings_text_input();
            self.request_overlay_frame(OverlayId::Settings);
        }
    }

    fn draw_overlay(&mut self, id: OverlayId) {
        match id {
            OverlayId::Launcher => {
                let Some(launcher) = &mut self.overlays.launcher else { return };
                let animating = launcher.draw(&self.egl, &mut self.renderer, &self.compositor);
                if animating {
                    launcher.request_frame(&self.qh);
                }
                self.settle_launcher();
            }
            OverlayId::Logout => {
                let Some(logout) = &mut self.overlays.logout else { return };
                let by_widget = logout.opened_by_widget;
                if logout.draw(&self.egl, &mut self.renderer, &self.compositor) {
                    logout.request_frame(&self.qh);
                }
                // The bar's logout item stops showing as open.
                if by_widget && !logout.opened_by_widget {
                    self.redraw.all();
                }
            }
            OverlayId::Overview => {
                let Some(overview) = &mut self.overlays.overview else { return };
                if overview.draw(&self.egl, &mut self.renderer, &self.compositor, &mut self.capture, &self.toplevels, &self.qh) {
                    overview.request_frame(&self.qh);
                }
            }
            OverlayId::WallpaperSelect => {
                let Some(picker) = &mut self.overlays.wallpaper_select else { return };
                if picker.draw(&self.egl, &mut self.renderer, &self.compositor) {
                    picker.request_frame(&self.qh);
                }
            }
            OverlayId::Polkit => {
                let service = self.core.services.as_ref().and_then(|s| s.polkit.as_deref());
                let Some(card) = &mut self.overlays.polkit else { return };
                if card.draw(&self.egl, &mut self.renderer, &self.compositor, service) {
                    card.request_frame(&self.qh);
                }
            }
            OverlayId::Settings => {
                let Some(settings) = &mut self.overlays.settings else { return };
                if settings.draw(&self.egl, &mut self.renderer, &self.compositor) {
                    settings.request_frame(&self.qh);
                }
                // The caret moved, or the card closed.
                self.sync_settings_text_input();
            }
        }
    }

    fn request_overlay_frame(&mut self, id: OverlayId) {
        let draw = match id {
            OverlayId::Launcher => self.overlays.launcher.as_mut().is_some_and(|l| l.request_frame(&self.qh)),
            OverlayId::Logout => self.overlays.logout.as_mut().is_some_and(|l| l.request_frame(&self.qh)),
            OverlayId::Overview => self.overlays.overview.as_mut().is_some_and(|o| o.request_frame(&self.qh)),
            OverlayId::WallpaperSelect => self.overlays.wallpaper_select.as_mut().is_some_and(|p| p.request_frame(&self.qh)),
            OverlayId::Settings => self.overlays.settings.as_mut().is_some_and(|s| s.request_frame(&self.qh)),
            OverlayId::Polkit => self.overlays.polkit.as_mut().is_some_and(|p| p.request_frame(&self.qh)),
        };
        if draw {
            self.draw_overlay(id);
        }
    }

    /// Pointer events on an overlay surface.
    fn overlay_pointer(&mut self, id: OverlayId, event: &PointerEvent) {
        let (x, y) = event.position;
        match id {
            OverlayId::Launcher => {
                let Some(launcher) = &mut self.overlays.launcher else { return };
                let (x, y) = (x as f32, y as f32);
                let repaint = match event.kind {
                    PointerEventKind::Enter { .. } | PointerEventKind::Motion { .. } => launcher.pointer(Some((x, y))),
                    PointerEventKind::Leave { .. } => launcher.pointer(None),
                    PointerEventKind::Press { button, .. } if button_from_code(button) == Button::Left => {
                        launcher.press(&self.compositor, x, y);
                        true
                    }
                    PointerEventKind::Release { .. } => {
                        launcher.release();
                        false
                    }
                    _ => false,
                };
                self.pointer.set_cursor(launcher.wants_hand());
                self.sync_launcher_text_input();
                if !repaint {
                    return;
                }
            }
            OverlayId::Logout => {
                let Some(logout) = &mut self.overlays.logout else { return };
                match event.kind {
                    PointerEventKind::Enter { .. } | PointerEventKind::Motion { .. } => {
                        logout.pointer(Some((x, y)));
                    }
                    PointerEventKind::Leave { .. } => {
                        logout.pointer(None);
                    }
                    PointerEventKind::Press { button, .. } if button_from_code(button) == Button::Left => logout.click(&self.compositor, x, y),
                    _ => return,
                }
                self.pointer.set_cursor(logout.wants_hand());
            }
            OverlayId::Overview => {
                let Some(overview) = &mut self.overlays.overview else { return };
                match event.kind {
                    PointerEventKind::Enter { .. } | PointerEventKind::Motion { .. } => overview.pointer(Some((x, y))),
                    PointerEventKind::Leave { .. } => overview.pointer(None),
                    PointerEventKind::Press { button, .. } if button_from_code(button) == Button::Left => overview.press(&self.compositor, x, y, self.modifiers.shift, self.modifiers.alt),
                    PointerEventKind::Release { .. } => overview.release(),
                    _ => return,
                }
                self.pointer.set_cursor(overview.wants_hand());
            }
            OverlayId::WallpaperSelect => {
                let Some(picker) = &mut self.overlays.wallpaper_select else { return };
                match event.kind {
                    PointerEventKind::Enter { .. } | PointerEventKind::Motion { .. } => picker.pointer(Some((x, y))),
                    PointerEventKind::Leave { .. } => picker.pointer(None),
                    PointerEventKind::Press { button, .. } if button_from_code(button) == Button::Left => picker.press(&self.compositor, x, y),
                    PointerEventKind::Axis { vertical, .. } if vertical.absolute != 0.0 => picker.wheel(vertical.absolute),
                    _ => return,
                }
                self.pointer.set_cursor(picker.wants_hand());
            }
            // The card takes keys only.
            OverlayId::Polkit => return,
            OverlayId::Settings => {
                let Some(settings) = &mut self.overlays.settings else { return };
                match event.kind {
                    PointerEventKind::Enter { .. } | PointerEventKind::Motion { .. } => settings.pointer(Some((x, y))),
                    PointerEventKind::Leave { .. } => settings.pointer(None),
                    PointerEventKind::Press { button, .. } if button_from_code(button) == Button::Left => settings.press(&self.compositor, x, y),
                    PointerEventKind::Release { .. } => settings.release(),
                    PointerEventKind::Axis { vertical, .. } if vertical.absolute != 0.0 => settings.wheel(vertical.absolute),
                    _ => return,
                }
                self.pointer.set_cursor(settings.wants_hand());
                self.sync_settings_text_input();
            }
        }
        self.request_overlay_frame(id);
    }

    /// The monitor an overlay opens on now, by `select_output`.
    fn overlay_target(&self) -> Option<usize> {
        let names: Vec<&str> = self.monitors.iter().map(|m| m.name.as_str()).collect();
        let focused = self.core.services.as_ref().map(|s| s.compositor.state().focused_monitor.clone()).unwrap_or_default();
        let hint = self.pointer_output.as_ref().and_then(|o| self.monitors.iter().position(|m| &m.output == o));
        overlays::select_output(&names, &focused, hint)
    }

    /// An input-method edit for the field on `surface`; `commit` sends its new caret back.
    fn text_input_apply(&mut self, surface: &WlSurface, edit: &TextInputEdit, commit: bool) {
        if let Some(launcher) = self.overlays.launcher.as_mut().filter(|l| l.overlay.owns(surface)) {
            edit.apply(&mut launcher.model);
            if commit {
                self.text_input.commit_after_done(launcher.text_input_state());
            }
            self.request_overlay_frame(OverlayId::Launcher);
        } else if let Some(settings) = self.overlays.settings.as_mut().filter(|s| s.overlay.owns(surface)) {
            edit.apply(&mut settings.model);
            if commit {
                self.text_input.commit_after_done(settings.text_input_state());
            }
            self.request_overlay_frame(OverlayId::Settings);
        }
    }

    /// The input method left the field on `surface`: drop its preedit.
    fn text_input_reset(&mut self, surface: &WlSurface) {
        if let Some(launcher) = self.overlays.launcher.as_mut().filter(|l| l.overlay.owns(surface)) {
            launcher.model.clear_preedit();
            self.request_overlay_frame(OverlayId::Launcher);
        } else if let Some(settings) = self.overlays.settings.as_mut().filter(|s| s.overlay.owns(surface)) {
            settings.model.set_preedit("");
            self.request_overlay_frame(OverlayId::Settings);
        }
    }

    /// Drops pointer and keyboard focus on surfaces about to be destroyed, so late events find nothing.
    pub(crate) fn forget_surfaces(&mut self, gone: &[wayland_client::backend::ObjectId]) {
        self.pointer.forget(gone);
        if self.keyboard_focus.as_ref().is_some_and(|s| gone.contains(&s.id())) {
            self.keyboard_focus = None;
        }
    }

    fn key_event(&mut self, keysym: Keysym, utf8: Option<&str>) {
        if let Some(event) = translate_key(keysym, utf8, self.modifiers) {
            self.key(event);
        }
    }
}

impl SeatHandler for State {
    fn seat_state(&mut self) -> &mut SeatState {
        &mut self.seat
    }

    fn new_seat(&mut self, _: &Connection, _: &QueueHandle<Self>, _: WlSeat) {}

    fn new_capability(&mut self, _: &Connection, qh: &QueueHandle<Self>, seat: WlSeat, capability: Capability) {
        self.wl_seat.get_or_insert_with(|| seat.clone());
        if let Some(manager) = &self.text_input_manager {
            self.text_input.bind(manager, &seat, qh);
        }
        if let Some(clipboard) = &self.clipboard {
            clipboard.bind_seat(&seat);
        }
        match capability {
            Capability::Pointer if self.wl_pointer.is_none() => match self.seat.get_pointer(qh, &seat) {
                Ok(pointer) => {
                    self.pointer.shape_device = self.cursor_shape.as_ref().map(|m| m.get_shape_device(&pointer, qh));
                    self.wl_pointer = Some(pointer);
                }
                Err(e) => ::log::error!("input: no pointer: {e}"),
            },
            Capability::Keyboard if self.keyboard.is_none() => {
                let repeat = Box::new(|state: &mut State, _: &WlKeyboard, event: SctkKeyEvent| state.key_event(event.keysym, event.utf8.as_deref()));
                match self.seat.get_keyboard_with_repeat(qh, &seat, None, self.handle.clone(), repeat) {
                    Ok(keyboard) => self.keyboard = Some(keyboard),
                    Err(e) => ::log::error!("input: no keyboard: {e}"),
                }
            }
            _ => {}
        }
    }

    fn remove_capability(&mut self, _: &Connection, _: &QueueHandle<Self>, _: WlSeat, capability: Capability) {
        match capability {
            Capability::Pointer => {
                if let Some(device) = self.pointer.shape_device.take() {
                    device.destroy();
                }
                if let Some(pointer) = self.wl_pointer.take() {
                    pointer.release();
                }
                self.pointer = Pointer::default();
            }
            Capability::Keyboard => {
                if let Some(keyboard) = self.keyboard.take() {
                    keyboard.release();
                }
            }
            _ => {}
        }
    }

    fn remove_seat(&mut self, _: &Connection, _: &QueueHandle<Self>, seat: WlSeat) {
        if self.wl_seat.as_ref() == Some(&seat) {
            self.wl_seat = None;
        }
    }
}

impl PointerHandler for State {
    fn pointer_frame(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &WlPointer, events: &[PointerEvent]) {
        for event in events {
            let (x, y) = event.position;
            // Events for a surface already destroyed find no monitor and only update the pointer.
            let monitor = self.monitor_for_surface(&event.surface);
            match event.kind {
                PointerEventKind::Enter { serial } => self.pointer.enter(&event.surface, x, y, serial),
                PointerEventKind::Leave { .. } => self.pointer.leave(&event.surface),
                PointerEventKind::Motion { .. } => (self.pointer.x, self.pointer.y) = (x, y),
                PointerEventKind::Press { serial, .. } | PointerEventKind::Release { serial, .. } => self.pointer.button_serial = serial,
                PointerEventKind::Axis { .. } => {}
            }
            if self.lock.owns_surface(&event.surface) {
                if let PointerEventKind::Press { button: BTN_LEFT, .. } = event.kind {
                    self.lock_handle_click(&event.surface, x, y);
                }
                continue;
            }
            if let Some(id) = self.overlays.owner_of(&event.surface) {
                self.overlay_pointer(id, event);
                continue;
            }
            let Some(i) = monitor else { continue };
            self.pointer_output = Some(self.monitors[i].output.clone());
            if self.monitors[i].wallpaper.as_ref().is_some_and(|w| w.owns(&event.surface)) {
                self.wallpaper_pointer(i, event);
                continue;
            }
            if self.monitors[i].notifications.as_ref().is_some_and(|n| n.owns(&event.surface)) {
                self.notification_pointer(i, event);
                continue;
            }
            if !self.monitors[i].bar.as_ref().is_some_and(|b| b.window.owns(&event.surface)) {
                if let Some(hand) = self.panel_pointer(i, event) {
                    self.pointer.set_cursor(hand);
                }
                continue;
            }
            let Some(hand) = self.bar_pointer(i, event) else { continue };
            if let Some(hand) = hand {
                self.pointer.set_cursor(hand);
            }
            self.request_frame(i);
        }
    }
}

impl KeyboardHandler for State {
    fn enter(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &WlKeyboard, surface: &WlSurface, _: u32, _: &[u32], _: &[Keysym]) {
        self.keyboard_focus = Some(surface.clone());
        self.text_input.keyboard_focus(surface, true);
    }

    fn leave(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &WlKeyboard, surface: &WlSurface, _: u32) {
        if self.keyboard_focus.as_ref() == Some(surface) {
            self.keyboard_focus = None;
        }
        self.text_input.keyboard_focus(surface, false);
    }

    fn press_key(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &WlKeyboard, serial: u32, event: SctkKeyEvent) {
        if let Some(clipboard) = &self.clipboard {
            clipboard.serial.set(serial);
        }
        self.key_event(event.keysym, event.utf8.as_deref());
    }

    fn repeat_key(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &WlKeyboard, _: u32, event: SctkKeyEvent) {
        self.key_event(event.keysym, event.utf8.as_deref());
    }

    fn release_key(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &WlKeyboard, _: u32, _: SctkKeyEvent) {}

    fn update_modifiers(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &WlKeyboard, _: u32, modifiers: Modifiers, _: RawModifiers, _: u32) {
        self.modifiers = modifiers;
    }
}

impl CompositorHandler for State {
    fn scale_factor_changed(&mut self, _: &Connection, qh: &QueueHandle<Self>, surface: &WlSurface, new_factor: i32) {
        if self.lock_scale_changed(surface, new_factor) || self.lock_transition_scale_changed(surface, new_factor) {
            return;
        }
        if let Some(id) = self.windows.owner_of(surface) {
            self.window_scale_changed(id, new_factor);
            return;
        }
        if let Some(id) = self.overlays.owner_of(surface) {
            if let Some(overlay) = self.overlays.get_mut(id) {
                overlay.set_scale(new_factor);
            }
            self.request_overlay_frame(id);
            return;
        }
        let Some(i) = self.monitor_for_surface(surface) else { return };
        if let Some(n) = self.monitors[i].notifications.as_mut().filter(|n| n.owns(surface)) {
            n.window.set_scale(new_factor);
            self.request_notification_frame(i);
            return;
        }
        if let Some(osd) = self.monitors[i].osd.as_mut().filter(|o| o.owns(surface)) {
            osd.window.set_scale(new_factor);
            self.request_osd_frame_only(i);
            return;
        }
        if self.monitors[i].wallpaper.as_ref().is_some_and(|w| w.owns(surface)) {
            let cfg = self.config();
            if let Some(wallpaper) = &mut self.monitors[i].wallpaper {
                wallpaper.set_scale(&self.renderer, &cfg, new_factor);
            }
            self.request_wallpaper_frame(i);
            return;
        }
        let (monitors, mut ctx) = self.split();
        let Some(bar) = &mut monitors[i].bar else { return };
        if !bar.window.owns(surface) {
            bar.panels.set_scale(&mut ctx, surface, new_factor);
            return;
        }
        bar.window.set_scale(new_factor);
        let due = bar.window.request_frame(qh);
        self.monitors[i].scale = new_factor;
        if due {
            self.draw(i);
        }
    }

    fn transform_changed(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &WlSurface, _: Transform) {}

    fn frame(&mut self, _: &Connection, qh: &QueueHandle<Self>, surface: &WlSurface, _: u32) {
        if self.lock_frame_done(surface) || self.lock_transition_frame_done(surface) {
            return;
        }
        if let Some(id) = self.windows.owner_of(surface) {
            self.window_frame_done(id);
            return;
        }
        // A late `done` for a removed output or popup finds no surface and is ignored.
        if let Some(id) = self.overlays.owner_of(surface) {
            if self.overlays.get_mut(id).is_some_and(|o| o.frame_done(qh)) {
                self.draw_overlay(id);
            }
            return;
        }
        let Some(i) = self.monitor_for_surface(surface) else { return };
        if let Some(wallpaper) = self.monitors[i].wallpaper.as_mut().filter(|w| w.owns(surface)) {
            if wallpaper.window.frame_done(qh) {
                self.draw_wallpaper(i);
            }
            return;
        }
        if let Some(n) = self.monitors[i].notifications.as_mut().filter(|n| n.owns(surface)) {
            if n.window.frame_done(qh) {
                self.draw_notifications(i);
            }
            return;
        }
        if let Some(osd) = self.monitors[i].osd.as_mut().filter(|o| o.owns(surface)) {
            if osd.window.frame_done(qh) {
                self.draw_osd(i);
            }
            return;
        }
        let (monitors, mut ctx) = self.split();
        let Some(bar) = &mut monitors[i].bar else { return };
        if !bar.window.owns(surface) {
            bar.panels.frame_done(&mut ctx, surface);
            return;
        }
        if bar.window.frame_done(qh) {
            self.draw(i);
        }
    }

    fn surface_enter(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &WlSurface, _: &WlOutput) {}

    fn surface_leave(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &WlSurface, _: &WlOutput) {}
}

impl OutputHandler for State {
    fn output_state(&mut self) -> &mut OutputState {
        &mut self.outputs
    }

    fn new_output(&mut self, _: &Connection, qh: &QueueHandle<Self>, output: WlOutput) {
        let info = self.outputs.info(&output);
        let name = info.as_ref().and_then(|i| i.name.clone()).unwrap_or_else(|| "?".into());
        let scale = info.as_ref().map_or(1, |i| i.scale_factor);
        if self.core.services.is_none() {
            return;
        }
        let cfg = self.config();
        let redraw = self.redraw.clone();
        let mut wallpaper = Wallpaper::new(&self.compositor, &self.layer_shell, qh, &output, &name, Rc::new(move || redraw.wallpaper()));
        wallpaper.window.set_scale(scale);
        let mut osd = Osd::new(&self.compositor, &self.layer_shell, qh, &output);
        osd.window.set_scale(scale);
        self.output_names.borrow_mut().push(name.clone());
        self.monitors.push(Monitor { output, name, scale, bar: None, wallpaper: Some(wallpaper), osd: Some(osd), notifications: None });
        let index = self.monitors.len() - 1;
        self.sync_bar_surface(index, &cfg);
        self.sync_notification_surface(index, &cfg);
        let output = self.monitors[index].output.clone();
        let name = self.monitors[index].name.clone();
        self.lock_hotplug_add(&output, &name);
    }

    fn update_output(&mut self, _: &Connection, _: &QueueHandle<Self>, _: WlOutput) {}

    fn output_destroyed(&mut self, _: &Connection, _: &QueueHandle<Self>, output: WlOutput) {
        self.lock_transition_output_removed(&output);
        self.lock_hotplug_remove(&output);
        let launcher_surface = self.overlays.launcher.as_ref().and_then(|l| l.overlay.surface());
        let settings_surface = self.overlays.settings.as_ref().and_then(|s| s.overlay.surface());
        let gone = self.overlays.release_output(&self.egl, &output);
        if let Some(surface) = launcher_surface.filter(|s| gone.contains(&s.id())) {
            self.text_input.clear_focused(&surface);
            self.sync_launcher_text_input();
        }
        if let Some(surface) = settings_surface.filter(|s| gone.contains(&s.id())) {
            self.text_input.clear_focused(&surface);
            self.sync_settings_text_input();
        }
        self.forget_surfaces(&gone);
        if self.pointer_output.as_ref() == Some(&output) {
            self.pointer_output = None;
        }
        let Some(i) = self.monitors.iter().position(|m| m.output == output) else { return };
        let monitor = self.monitors.remove(i);
        ::log::info!("bar: removed from '{}'", monitor.name);
        self.output_names.borrow_mut().retain(|n| *n != monitor.name);
        if let Some(wallpaper) = monitor.wallpaper {
            self.forget_surfaces(&[wallpaper.surface_id()]);
            wallpaper.destroy(&self.egl, &self.renderer);
        }
        if let Some(n) = monitor.notifications {
            self.forget_surfaces(&[n.surface_id()]);
            n.destroy(&self.egl, &self.renderer);
        }
        if let Some(osd) = monitor.osd {
            self.forget_surfaces(&[osd.surface_id()]);
            osd.destroy(&self.egl, &self.renderer);
        }
        if let Some(bar) = monitor.bar {
            self.forget_surfaces(&bar.surface_ids());
            bar.destroy(&self.egl, &self.renderer);
        }
    }
}

impl LayerShellHandler for State {
    /// Only output removal destroys a bar.
    fn closed(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &LayerSurface) {}

    fn configure(&mut self, _: &Connection, qh: &QueueHandle<Self>, layer: &LayerSurface, configure: LayerSurfaceConfigure, _: u32) {
        if self.lock_transition_configure(layer, configure.new_size) {
            return;
        }
        if let Some(id) = self.overlays.owner_of(layer.wl_surface()) {
            let (width, height) = (configure.new_size.0 as i32, configure.new_size.1 as i32);
            match id {
                OverlayId::Launcher => {
                    if let Some(launcher) = &mut self.overlays.launcher {
                        launcher.configure(&self.egl, &self.compositor, width, height);
                    }
                    self.sync_launcher_text_input();
                }
                OverlayId::Logout => {
                    if let Some(logout) = &mut self.overlays.logout {
                        logout.configure(&self.egl, &self.compositor, width, height);
                    }
                }
                OverlayId::Overview => {
                    if let Some(overview) = &mut self.overlays.overview {
                        overview.configure(&self.egl, &self.compositor, width, height);
                    }
                }
                OverlayId::WallpaperSelect => {
                    if let Some(picker) = &mut self.overlays.wallpaper_select {
                        picker.configure(&self.egl, &self.compositor, width, height);
                    }
                }
                OverlayId::Settings => {
                    if let Some(settings) = &mut self.overlays.settings {
                        settings.configure(&self.egl, &self.compositor, width, height);
                    }
                }
                OverlayId::Polkit => {
                    if let Some(card) = &mut self.overlays.polkit {
                        card.configure(&self.egl, &self.compositor, width, height);
                    }
                }
            }
            self.request_overlay_frame(id);
            return;
        }
        let Some(i) = self.monitor_for_surface(layer.wl_surface()) else { return };
        let (width, height) = (configure.new_size.0 as i32, configure.new_size.1 as i32);
        if let Some(n) = self.monitors[i].notifications.as_mut().filter(|n| n.owns(layer.wl_surface())) {
            n.configure(&self.egl, width);
            self.request_notification_frame(i);
            return;
        }
        if let Some(osd) = self.monitors[i].osd.as_mut().filter(|o| o.owns(layer.wl_surface())) {
            osd.configure(&self.egl, width);
            return;
        }
        if self.monitors[i].wallpaper.as_ref().is_some_and(|w| w.owns(layer.wl_surface())) {
            let cfg = self.config();
            if let Some(wallpaper) = &mut self.monitors[i].wallpaper {
                wallpaper.configure(&self.egl, &self.renderer, &cfg, width, height);
            }
            self.request_wallpaper_frame(i);
            return;
        }
        let Some(bar) = &mut self.monitors[i].bar else { return };
        if bar.panels.owns_overlay(layer.wl_surface()) {
            bar.panels.configure(&self.egl, width, height);
            return;
        }
        bar.configure(&self.egl, width);
        let _ = qh;
        self.request_frame(i);
    }
}

impl PopupHandler for State {
    fn configure(&mut self, _: &Connection, _: &QueueHandle<Self>, popup: &Popup, config: PopupConfigure) {
        let (monitors, mut ctx) = self.split();
        for bar in monitors.iter_mut().filter_map(|m| m.bar.as_mut()) {
            if bar.panels.popup_configure(&mut ctx, popup, config.width, config.height) {
                return;
            }
        }
    }

    fn done(&mut self, _: &Connection, _: &QueueHandle<Self>, popup: &Popup) {
        let gone = popup.wl_surface().id();
        let (monitors, mut ctx) = self.split();
        for bar in monitors.iter_mut().filter_map(|m| m.bar.as_mut()) {
            if bar.panels.popup_done(&mut ctx, popup) {
                break;
            }
        }
        self.forget_surfaces(&[gone]);
    }
}

impl ProvidesRegistryState for State {
    fn registry(&mut self) -> &mut RegistryState {
        &mut self.registry
    }

    registry_handlers![OutputState, SeatState];
}

delegate_compositor!(State);
smithay_client_toolkit::delegate_session_lock!(State);
delegate_output!(State);
delegate_layer!(State);
delegate_registry!(State);
delegate_seat!(State);
delegate_pointer!(State);
delegate_keyboard!(State);
// Only the globals `XdgShell::bind` binds: the shell answers pings; there are no toplevels to decorate.
smithay_client_toolkit::delegate_xdg_shell!(State);
delegate_xdg_popup!(State);
delegate_xdg_window!(State);
delegate_data_device!(State);

impl DataDeviceHandler for State {
    fn enter(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &WlDataDevice, _: f64, _: f64, _: &WlSurface) {}
    fn leave(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &WlDataDevice) {}
    fn motion(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &WlDataDevice, _: f64, _: f64) {}
    /// The selection is read on demand, by `Clipboard::request`.
    fn selection(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &WlDataDevice) {}
    fn drop_performed(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &WlDataDevice) {}
}

impl DataOfferHandler for State {
    fn source_actions(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &mut DragOffer, _: DndAction) {}
    fn selected_action(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &mut DragOffer, _: DndAction) {}
}

impl DataSourceHandler for State {
    fn accept_mime(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &WlDataSource, _: Option<String>) {}

    fn send_request(&mut self, _: &Connection, _: &QueueHandle<Self>, source: &WlDataSource, _: String, pipe: WritePipe) {
        if let Some(clipboard) = &self.clipboard {
            clipboard.send(source, pipe);
        }
    }

    fn cancelled(&mut self, _: &Connection, _: &QueueHandle<Self>, source: &WlDataSource) {
        if let Some(clipboard) = &self.clipboard {
            clipboard.cancelled(source);
        }
    }

    fn dnd_dropped(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &WlDataSource) {}
    fn dnd_finished(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &WlDataSource) {}
    fn action(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &WlDataSource, _: DndAction) {}
}

impl Dispatch<ZwpTextInputManagerV3, ()> for State {
    fn event(_: &mut Self, _: &ZwpTextInputManagerV3, _: <ZwpTextInputManagerV3 as Proxy>::Event, _: &(), _: &Connection, _: &QueueHandle<Self>) {}
}

impl Dispatch<ZwpTextInputV3, ()> for State {
    fn event(state: &mut Self, _: &ZwpTextInputV3, event: zwp_text_input_v3::Event, _: &(), _: &Connection, _: &QueueHandle<Self>) {
        use zwp_text_input_v3::Event;
        let text_input = &mut state.text_input;
        match event {
            Event::Enter { surface } => text_input.enter(&surface),
            Event::Leave { surface } => {
                if text_input.leave(&surface) {
                    state.text_input_reset(&surface);
                }
            }
            Event::PreeditString { text, .. } => text_input.pending().preedit(text.as_deref().unwrap_or("")),
            Event::CommitString { text } => text_input.pending().commit(text.as_deref().unwrap_or("")),
            Event::DeleteSurroundingText { before_length, after_length } => text_input.pending().delete(before_length, after_length),
            Event::Done { serial } => {
                if let Some((surface, edit, commit)) = text_input.done(serial) {
                    state.text_input_apply(&surface, &edit, commit);
                }
            }
            _ => {}
        }
    }
}
