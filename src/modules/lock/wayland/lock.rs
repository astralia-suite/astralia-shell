//! The session lock: one `ext-session-lock` surface per output, PAM off the loop, and the unlock.

use std::panic::AssertUnwindSafe;
use std::rc::Rc;
use std::time::Instant;

use smithay_client_toolkit::session_lock::{SessionLock, SessionLockHandler, SessionLockSurface, SessionLockSurfaceConfigure};
use smithay_client_toolkit::shell::WaylandSurface;
use wayland_client::protocol::wl_callback::{self, WlCallback};
use wayland_client::protocol::wl_output::WlOutput;
use wayland_client::protocol::wl_surface::WlSurface;
use wayland_client::{Connection, Dispatch, Proxy, QueueHandle};

use crate::backend::wayland::State;
use crate::backend::wayland::render::gl_canvas::GlCanvas;
use crate::backend::wayland::render::layer_window::LayerWindow;
use crate::base::input::KeyEvent;
use crate::base::wall_clock::{format_tm, now_tm};
use crate::config::settings::lock_enabled_for;
use crate::modules::lock::layout::{card_height, card_width, dev_unlock_hit};
use crate::modules::lock::model::{Hits, Key, Model, Motion, motion_clear_dots, motion_fit, motion_type};
use crate::modules::lock::pam;
use crate::modules::lock::view::{self, Info};
use crate::modules::lock::wayland::wallpaper::Wallpaper;
use crate::render::geometry::Rect;
use crate::render::text_field::text_field_wipe;
use crate::render::theme::palette;
use crate::service::runtime;
use crate::service::telemetry::{cpu_temp_poll, gpu_temp_poll, system_stats_poll};
use crate::service::user::{face_path, login_name, os_name, uptime, user_name};

const SLOW_FRAME_MS: f32 = 50.0;
const GPU_POLL_EVERY: u32 = 5;

/// What an authentication result does once it reaches the loop.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AuthOutcome {
    Ignore,
    Unlock,
    Failed,
}

/// Fail closed: only the current authentication's success, while the session is locked, unlocks.
pub fn deliver_auth(locked: bool, model: &mut Model, generation: u64, success: bool) -> AuthOutcome {
    if !locked {
        return AuthOutcome::Ignore;
    }
    let current = generation == model.generation() && model.authenticating();
    if model.finish_auth(generation, success) {
        return AuthOutcome::Unlock;
    }
    if current { AuthOutcome::Failed } else { AuthOutcome::Ignore }
}

/// A lock surface as the role of a `LayerWindow`; dropping it destroys the lock surface, then the `wl_surface`.
pub struct LockRole(SessionLockSurface);

impl WaylandSurface for LockRole {
    fn wl_surface(&self) -> &WlSurface {
        self.0.wl_surface()
    }
}

/// Marks the `wl_display.sync` that follows `unlock_and_destroy`.
pub struct UnlockSync;

pub struct OutputSurface {
    pub output: WlOutput,
    pub name: String,
    pub window: LayerWindow<LockRole>,
    canvas: GlCanvas,
    pub motion: Motion,
    hits: Hits,
    /// The card is off on this output; only the wallpaper shows.
    panel_gated: bool,
}

pub struct Lock {
    session: Option<SessionLock>,
    pub active: bool,
    pub locked: bool,
    pub unlocking: bool,
    pub acknowledge_startup: bool,
    pub surfaces: Vec<OutputSurface>,
    pub model: Model,
    pub wallpaper: Wallpaper,
    pub user: String,
    poll_tick: u32,
    unlock_sync: Option<WlCallback>,
    pub transition: crate::modules::lock::wayland::transition::Transition,
}

impl Lock {
    pub fn new(wallpaper_ready: Rc<dyn Fn()>) -> Lock {
        Lock {
            session: None,
            active: false,
            locked: false,
            unlocking: false,
            acknowledge_startup: false,
            surfaces: Vec::new(),
            model: Model::default(),
            wallpaper: Wallpaper::new(wallpaper_ready),
            user: String::new(),
            poll_tick: 0,
            unlock_sync: None,
            transition: Default::default(),
        }
    }

    pub fn is_active(&self) -> bool {
        self.active
    }

    pub fn has_session(&self) -> bool {
        self.session.is_some()
    }

    pub fn owns_surface(&self, surface: &WlSurface) -> bool {
        self.surfaces.iter().any(|s| s.window.owns(surface))
    }

    fn index_of(&self, surface: &WlSurface) -> Option<usize> {
        self.surfaces.iter().position(|s| s.window.owns(surface))
    }
}

/// Where `key` sits among the surfaces' keys; adding a known output and removing an unknown one are no-ops.
pub fn surface_slot<'a, K: PartialEq + 'a>(keys: impl Iterator<Item = &'a K>, key: &K) -> Option<usize> {
    keys.into_iter().position(|k| k == key)
}

/// Builds the card's `Info` from the services for the length of `f`.
pub fn with_info<R>(services: Option<&crate::service::Services>, user: &str, f: impl FnOnce(&Info) -> R) -> R {
    let battery = services.and_then(|s| s.battery.as_ref()).map(|b| b.status());
    let media = services.and_then(|s| s.media.as_ref()).map(|m| m.status());
    let notifications = services.map(|s| s.notifications.list());
    let telemetry = services.map(|s| s.telemetry.borrow());
    let tm = now_tm();
    let info = Info {
        user: user.to_string(),
        os: os_name(),
        wm: services.map_or("", |s| s.compositor.name()).to_string(),
        uptime: uptime(),
        hour: format_tm(&tm, "%H"),
        minute: format_tm(&tm, "%M"),
        date: format_tm(&tm, "%a %Y-%m-%d").to_uppercase(),
        face: face_path(),
        battery: battery.as_ref(),
        media: media.as_deref(),
        stats: telemetry.as_ref().map(|t| &t.stats),
        cpu_temp: telemetry.as_ref().map(|t| &t.cpu),
        gpu_temp: telemetry.as_ref().map(|t| &t.gpu),
        notifications: notifications.as_ref().map(|n| n.as_slice()),
    };
    f(&info)
}

fn hit(r: &Rect, x: f64, y: f64) -> bool {
    let (x, y) = (x as f32, y as f32);
    r.w > 0.0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h
}

impl State {
    /// Starts the lock now, without the transition. False when the compositor cannot lock.
    pub fn lock_request(&mut self) -> bool {
        if self.lock.active {
            return true;
        }
        let session = match self.session_lock.lock(&self.qh) {
            Ok(session) => session,
            Err(_) => {
                ::log::error!("lock: compositor has no ext_session_lock_manager_v1");
                return false;
            }
        };
        let cfg = self.config();
        self.lock.session = Some(session);
        self.lock.active = true;
        self.lock.locked = false;
        self.lock.unlocking = false;
        self.lock.model.reset();
        if cfg.lock_wallpaper_custom && !self.lock.wallpaper.loaded() {
            self.lock.wallpaper.load();
        }
        if self.lock.user.is_empty() {
            self.lock.user = user_name();
        }
        let outputs: Vec<(WlOutput, String)> = self.monitors.iter().map(|m| (m.output.clone(), m.name.clone())).collect();
        for (output, name) in outputs {
            self.lock_create_surface(&output, &name);
        }
        ::log::info!("lock: requested ({} surface(s))", self.lock.surfaces.len());
        true
    }

    fn lock_create_surface(&mut self, output: &WlOutput, name: &str) {
        let Some(session) = &self.lock.session else { return };
        let cfg = self.config();
        let surface = self.compositor.create_surface(&self.qh);
        let role = session.create_lock_surface(surface, output, &self.qh);
        let mut window = LayerWindow::new(LockRole(role), 0);
        let scale = self.monitors.iter().find(|m| m.output == *output).map_or(1, |m| m.scale);
        window.set_scale(scale);
        let mut canvas = GlCanvas::new();
        let handle = self.handle.clone();
        canvas.set_on_image_ready(Rc::new(move || {
            handle.insert_idle(|state: &mut State| state.lock_request_all());
        }));
        let panel_gated = !lock_enabled_for(&cfg, name);
        self.lock.surfaces.push(OutputSurface { output: output.clone(), name: name.to_string(), window, canvas, motion: Motion::default(), hits: Hits::default(), panel_gated });
    }

    fn lock_destroy_surface(&mut self, surface: OutputSurface) {
        let OutputSurface { window, canvas, .. } = surface;
        self.egl.make_current(None);
        canvas.destroy(&self.renderer);
        self.forget_surfaces(&[window.layer.wl_surface().id()]);
        window.destroy(&self.egl);
    }

    /// Locks, through the slash transition unless `animate` is false or animations are off.
    pub fn lock_begin(&mut self, animate: bool) {
        if self.lock.active || self.lock_transition_active() {
            return;
        }
        let cfg = self.config();
        if cfg.lock_wallpaper_custom && !self.lock.wallpaper.loaded() {
            self.lock.wallpaper.load();
        }
        if animate && !cfg.animations_disabled && !self.monitors.is_empty() && self.lock_transition_open_lock() {
            return;
        }
        if !self.lock_request() {
            self.lock_release_wallpaper_if_idle();
        }
    }

    /// Destroys every lock surface and resets the state; the session must already be gone.
    pub fn lock_teardown(&mut self) {
        let surfaces = std::mem::take(&mut self.lock.surfaces);
        for surface in surfaces {
            self.lock_destroy_surface(surface);
        }
        self.lock.unlock_sync = None;
        self.lock.active = false;
        self.lock.locked = false;
        self.lock.unlocking = false;
        self.session_locked = false;
        self.lock.model.reset();
        self.redraw.all();
        self.lock_release_wallpaper_if_idle();
    }

    pub fn lock_release_wallpaper_if_idle(&mut self) {
        if !self.lock.active && !self.lock_transition_active() && self.lock.wallpaper.loaded() {
            self.egl.make_current(None);
            self.lock.wallpaper.release(&self.renderer);
        }
    }

    /// Starts unlocking after a successful authentication (or the development corner).
    pub fn lock_begin_unlock(&mut self) {
        if self.lock.unlocking || !self.lock.locked {
            return;
        }
        self.lock.unlocking = true;
        if self.lock_transition_open_unlock() {
            return;
        }
        ::log::info!("lock: unlock begin (animated=false)");
        self.handle.insert_idle(|state: &mut State| state.lock_finish_unlock());
    }

    pub fn lock_finish_unlock(&mut self) {
        if self.lock.session.is_none() {
            return;
        }
        self.lock_release_session();
        self.lock_transition_stop();
    }

    /// `unlock_and_destroy`, then a `sync` whose `done` tears the surfaces down once the server has the unlock.
    pub fn lock_release_session(&mut self) {
        let Some(session) = self.lock.session.take() else { return };
        session.unlock();
        drop(session);
        self.lock.locked = false;
        self.lock.unlock_sync = Some(self.display.sync(&self.qh, UnlockSync));
    }

    fn lock_sync_done(&mut self) {
        if self.lock.unlock_sync.take().is_none() {
            return;
        }
        self.lock_teardown();
        ::log::info!("lock: session unlocked");
    }

    pub fn lock_handle_key(&mut self, event: &KeyEvent) {
        if !self.lock.locked || self.lock.unlocking {
            return;
        }
        match self.lock.model.key(event) {
            Key::Submit => self.lock_try_authenticate(),
            Key::Changed => {
                let lock = &mut self.lock;
                for surface in &mut lock.surfaces {
                    motion_type(&mut surface.motion, lock.model.password());
                }
                self.lock_request_all();
            }
            Key::None => {}
        }
    }

    fn lock_try_authenticate(&mut self) {
        let Some(mut password) = self.lock.model.begin_auth() else {
            ::log::info!("lock: submit ignored (authenticating={} empty={})", self.lock.model.authenticating(), self.lock.model.password().is_empty());
            return;
        };
        ::log::info!("lock: auth started");
        let generation = self.lock.model.generation();
        let handle = self.handle.clone();
        runtime::spawn(async move {
            let result = blocking::unblock(move || {
                let login = login_name();
                let result = std::panic::catch_unwind(AssertUnwindSafe(|| pam::authenticate(&login, &password, &pam::service_dir())));
                text_field_wipe(&mut password);
                result.unwrap_or_else(|_| pam::Result { success: false, message: "authentication panicked".into() })
            })
            .await;
            handle.insert_idle(move |state: &mut State| state.lock_deliver_auth(generation, &result));
        });
        self.lock_request_all();
    }

    fn lock_deliver_auth(&mut self, generation: u64, result: &pam::Result) {
        ::log::info!("lock: auth result success={} locked={}", result.success, self.lock.locked);
        match deliver_auth(self.lock.locked && !self.lock.unlocking, &mut self.lock.model, generation, result.success) {
            AuthOutcome::Unlock => self.lock_begin_unlock(),
            AuthOutcome::Failed => {
                if !result.message.is_empty() {
                    ::log::info!("lock: authentication failed: {}", result.message);
                }
                for surface in &mut self.lock.surfaces {
                    motion_clear_dots(&mut surface.motion);
                }
                self.lock_request_all();
            }
            AuthOutcome::Ignore => {}
        }
    }

    pub fn lock_handle_click(&mut self, surface: &WlSurface, x: f64, y: f64) {
        let Some(i) = self.lock.index_of(surface) else { return };
        let (height, hits) = (self.lock.surfaces[i].window.height, self.lock.surfaces[i].hits);
        if dev_unlock_hit(x, y, f64::from(height)) {
            self.lock_begin_unlock();
            return;
        }
        if self.lock.unlocking || !self.lock.locked {
            return;
        }
        if hit(&hits.pill_button, x, y) {
            self.lock_try_authenticate();
            return;
        }
        let Some(media) = self.core.services.as_ref().and_then(|s| s.media.clone()) else { return };
        if hit(&hits.media_prev, x, y) {
            media.previous();
        } else if hit(&hits.media_play, x, y) {
            media.play_pause();
        } else if hit(&hits.media_next, x, y) {
            media.next();
        } else {
            return;
        }
        self.lock_request_all();
    }

    /// Once a second: telemetry, the failure timer and the clock.
    pub fn lock_timer_tick(&mut self) {
        self.lock_transition_check_deadline();
        if !self.lock.active {
            return;
        }
        self.lock.poll_tick = self.lock.poll_tick.wrapping_add(1);
        if let Some(services) = &self.core.services {
            let mut t = services.telemetry.borrow_mut();
            cpu_temp_poll(&mut t.cpu);
            system_stats_poll(&mut t.stats);
            if self.lock.poll_tick.is_multiple_of(GPU_POLL_EVERY) || t.gpu.nvidia_smi_proc.running() {
                gpu_temp_poll(&mut t.gpu);
            }
        }
        self.lock.model.tick(Instant::now());
        self.lock_request_all();
    }

    /// `ASTRALIA_START_LOCKED=1` at login: lock at once, without the transition; `locked` writes the ack file.
    pub fn lock_at_startup(&mut self) {
        self.lock_poll_telemetry();
        self.lock.acknowledge_startup = true;
        self.lock_begin(false);
    }

    /// Polls telemetry once so the card opens with fresh figures.
    pub fn lock_poll_telemetry(&mut self) {
        if let Some(services) = &self.core.services {
            let mut t = services.telemetry.borrow_mut();
            cpu_temp_poll(&mut t.cpu);
            system_stats_poll(&mut t.stats);
            gpu_temp_poll(&mut t.gpu);
        }
    }

    /// A new output while locked gets its lock surface at once.
    pub fn lock_hotplug_add(&mut self, output: &WlOutput, name: &str) {
        if !self.lock.active || self.lock.session.is_none() || surface_slot(self.lock.surfaces.iter().map(|s| &s.output), output).is_some() {
            return;
        }
        self.lock_create_surface(output, name);
    }

    pub fn lock_hotplug_remove(&mut self, output: &WlOutput) {
        if let Some(name) = self.monitors.iter().find(|m| m.output == *output).map(|m| m.name.clone()) {
            self.egl.make_current(None);
            self.lock.wallpaper.output_removed(&self.renderer, &name);
        }
        let Some(i) = surface_slot(self.lock.surfaces.iter().map(|s| &s.output), output) else { return };
        let surface = self.lock.surfaces.remove(i);
        self.lock_destroy_surface(surface);
    }

    pub fn lock_request_all(&mut self) {
        for i in 0..self.lock.surfaces.len() {
            if self.lock.surfaces[i].window.request_frame(&self.qh) {
                self.lock_draw(i);
            }
        }
        self.lock_transition_request_all();
    }

    /// Repaints the lock surface on `output`, after its wallpaper changed.
    pub fn lock_request_output(&mut self, output: &str) {
        if let Some(i) = self.lock.surfaces.iter().position(|s| s.name == output)
            && self.lock.surfaces[i].window.request_frame(&self.qh)
        {
            self.lock_draw(i);
        }
    }

    /// A frame callback; true when the surface is a lock surface.
    pub fn lock_frame_done(&mut self, surface: &WlSurface) -> bool {
        let Some(i) = self.lock.index_of(surface) else { return false };
        if self.lock.surfaces[i].window.frame_done(&self.qh) {
            self.lock_draw(i);
        }
        true
    }

    pub fn lock_scale_changed(&mut self, surface: &WlSurface, scale: i32) -> bool {
        let Some(i) = self.lock.index_of(surface) else { return false };
        self.lock.surfaces[i].window.set_scale(scale);
        if self.lock.surfaces[i].window.request_frame(&self.qh) {
            self.lock_draw(i);
        }
        true
    }

    fn lock_draw(&mut self, i: usize) {
        let started = Instant::now();
        let cfg = self.config();
        let Some(surface) = self.lock.surfaces.get_mut(i) else { return };
        if !surface.window.begin(&self.egl, &mut self.renderer) {
            return;
        }
        let made = Instant::now();
        let (w, h, scale) = (surface.window.width, surface.window.height, surface.window.scale);
        self.renderer.draw_rect(0.0, 0.0, w as f32, h as f32, palette().base.rgba());
        surface.motion.animations.tick(made);
        if self.lock.wallpaper.loaded() {
            self.lock.wallpaper.draw(&cfg, &mut self.renderer, &surface.name, w, h, scale);
        } else if let Some(wallpaper) = self.monitors.iter().find(|m| m.name == surface.name).and_then(|m| m.wallpaper.as_ref()) {
            wallpaper.draw_columns(&mut self.renderer, w, h);
        }
        let wallpaper_done = Instant::now();

        surface.hits = Hits::default();
        if !surface.panel_gated {
            let oh = h as f32;
            motion_fit(&mut surface.motion, card_width(oh), card_height(oh));
            let mut frame = surface.canvas.begin(&mut self.renderer, scale);
            let model = &mut self.lock.model;
            let motion = &mut surface.motion;
            with_info(self.core.services.as_ref(), &self.lock.user, |info| view::paint(&mut frame, model, motion, info, w as f32, oh));
            frame.flush();
            surface.hits = *self.lock.model.hits();
        }
        let panel_done = Instant::now();
        surface.window.end(&self.egl);
        let swapped = Instant::now();

        let ms = |a: Instant, b: Instant| b.saturating_duration_since(a).as_secs_f32() * 1000.0;
        if ms(started, swapped) > SLOW_FRAME_MS {
            ::log::info!(
                "lock: SLOW frame '{}' make={:.1} wallpaper={:.1} panel={:.1} swap={:.1}",
                surface.name,
                ms(started, made),
                ms(made, wallpaper_done),
                ms(wallpaper_done, panel_done),
                ms(panel_done, swapped)
            );
        }
        if surface.motion.animations.has_active() {
            surface.window.request_frame(&self.qh);
        }
        self.egl.make_current(None);
    }
}

impl SessionLockHandler for State {
    fn locked(&mut self, _: &Connection, _: &QueueHandle<Self>, _: SessionLock) {
        if !self.lock.active {
            return;
        }
        if self.lock.acknowledge_startup {
            self.lock.acknowledge_startup = false;
            crate::app::session::start_locked_ack();
        }
        self.lock.locked = true;
        self.session_locked = true;
        ::log::info!("lock: session locked, {} surface(s)", self.lock.surfaces.len());
        for s in &self.lock.surfaces {
            ::log::info!("lock:   '{}' configured={} egl={} {}x{}", s.name, s.window.width > 0, s.window.egl_surface.is_some(), s.window.width, s.window.height);
        }
        self.lock_request_all();
        self.lock_transition_release_keyboard();
    }

    fn finished(&mut self, _: &Connection, _: &QueueHandle<Self>, session_lock: SessionLock) {
        let Some(session) = self.lock.session.take() else { return };
        ::log::info!("lock: compositor sent finished");
        session.unlock();
        drop(session);
        drop(session_lock);
        self.lock_teardown();
        self.lock_transition_stop();
    }

    fn configure(&mut self, _: &Connection, qh: &QueueHandle<Self>, surface: SessionLockSurface, configure: SessionLockSurfaceConfigure, _: u32) {
        let Some(i) = self.lock.index_of(surface.wl_surface()) else { return };
        let (w, h) = configure.new_size;
        let window = &mut self.lock.surfaces[i].window;
        window.height = h as i32;
        window.configure(&self.egl, w as i32);
        if window.egl_surface.is_none() {
            ::log::error!("lock: eglCreateWindowSurface failed on '{}'", self.lock.surfaces[i].name);
            return;
        }
        if window.request_frame(qh) {
            self.lock_draw(i);
        }
    }
}

impl Dispatch<WlCallback, UnlockSync> for State {
    fn event(state: &mut State, _: &WlCallback, event: wl_callback::Event, _: &UnlockSync, _: &Connection, _: &QueueHandle<State>) {
        if let wl_callback::Event::Done { .. } = event {
            state.lock_sync_done();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::base::input::{KeyEvent, KeyKind};

    fn typed(model: &mut Model) -> u64 {
        model.key(&KeyEvent { kind: KeyKind::Text, text: "pw".into(), ..Default::default() });
        model.begin_auth().expect("auth starts");
        model.generation()
    }

    #[test]
    fn lock_hotplug_surfaces() {
        let known = ["DP-1", "DP-2"];
        assert_eq!(surface_slot(known.iter(), &"DP-2"), Some(1));
        assert_eq!(surface_slot(known.iter(), &"HDMI-A-1"), None, "removing an unknown output is a no-op");
        assert!(surface_slot(known.iter(), &"DP-1").is_some(), "adding a known output is a no-op");
        assert_eq!(surface_slot(std::iter::empty::<&&str>(), &"DP-1"), None);
    }

    #[test]
    fn lock_auth_delivery() {
        let mut model = Model::default();
        let generation = typed(&mut model);
        assert_eq!(deliver_auth(false, &mut model, generation, true), AuthOutcome::Ignore, "nothing happens before `locked`");
        assert!(model.authenticating(), "and the model is untouched");

        assert_eq!(deliver_auth(true, &mut model, generation + 1, true), AuthOutcome::Ignore, "a stale generation never unlocks");
        assert_eq!(deliver_auth(true, &mut model, generation, false), AuthOutcome::Failed, "a current failure clears the dots");
        assert!(model.failed() && model.password().is_empty());
        assert_eq!(deliver_auth(true, &mut model, generation, true), AuthOutcome::Ignore, "a second result for that auth never unlocks");

        let generation = typed(&mut model);
        assert_eq!(deliver_auth(true, &mut model, generation, true), AuthOutcome::Unlock, "only the current success unlocks");
    }
}
