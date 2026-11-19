//! The slash transition over every output, in and out of the lock.

use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use smithay_client_toolkit::shell::WaylandSurface;
use smithay_client_toolkit::shell::wlr_layer::{Anchor, KeyboardInteractivity, Layer, LayerSurface};
use wayland_client::Proxy;
use wayland_client::protocol::wl_output::WlOutput;
use wayland_client::protocol::wl_surface::WlSurface;

use crate::backend::wayland::State;
use crate::backend::wayland::render::gl_canvas::{GlCanvas, GlFrame};
use crate::backend::wayland::render::layer_window::LayerWindow;
use crate::backend::wayland::render::overlay::set_input_region;
use crate::backend::wayland::render::thunder::{ThunderBurst, ThunderParams, ThunderShockParams};
use crate::config::lock_config as cfg;
use crate::config::settings::{Config, lock_enabled_for};
use crate::modules::lock::layout::{card_height, card_width};
use crate::modules::lock::model::{Model, Motion, motion_fit};
use crate::modules::lock::slash::{SlashPlan, SlashShock, SlashStrike, slash_run_progress};
use crate::modules::lock::transition_view::{Thunder, TransitionMode, paint_transition};
use crate::modules::lock::view;
use crate::modules::lock::wayland::lock::with_info;
use crate::modules::lock::wayland::wallpaper::Wallpaper;
use crate::render::theme::palette;
use crate::render::tokens::WHITE;

const LAYER_NAMESPACE: &str = "astralia-shell-lock-transition";
const TRANSITION_READY_MS: u64 = 400;
const TRANSITION_DEADLINE_MARGIN_MS: u64 = 1500;

/// What the transition's bookkeeping asks the host to do next.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Signal {
    /// Every surface holds the lock picture (`true`), or waiting gave up (`false`).
    Ready(bool),
    Finish,
}

/// The ready wait and the play deadline, apart from any surface.
#[derive(Debug, Default)]
pub struct Phase {
    ready_by: Option<Instant>,
    play_by: Option<Instant>,
}

impl Phase {
    pub fn wait_ready(&mut self, now: Instant) {
        self.ready_by = now.checked_add(Duration::from_millis(TRANSITION_READY_MS)).or(Some(now));
    }

    pub fn play(&mut self, now: Instant) {
        let span = Duration::from_millis(cfg::SLASH_MS as u64 + TRANSITION_DEADLINE_MARGIN_MS);
        self.play_by = now.checked_add(span).or(Some(now));
    }

    pub fn playing(&self) -> bool {
        self.play_by.is_some()
    }

    /// Ends the play; false when nothing was playing.
    pub fn finish(&mut self) -> bool {
        self.play_by.take().is_some()
    }

    pub fn stop(&mut self) {
        self.ready_by = None;
        self.play_by = None;
    }

    /// `Ready(true)` once, when every surface holds.
    pub fn take_ready(&mut self, held: &[bool]) -> Option<Signal> {
        if self.ready_by.is_none() || !held.iter().all(|&h| h) {
            return None;
        }
        self.ready_by = None;
        Some(Signal::Ready(true))
    }

    pub fn check_deadline(&mut self, now: Instant) -> Option<Signal> {
        if self.ready_by.is_some_and(|by| now > by) {
            self.ready_by = None;
            return Some(Signal::Ready(false));
        }
        if self.play_by.is_some_and(|by| now > by) {
            return Some(Signal::Finish);
        }
        None
    }

    /// A surface went away; `held` is what remains.
    pub fn removed(&mut self, held: &[bool], was_front: bool) -> Vec<Signal> {
        let mut out = Vec::new();
        if self.ready_by.is_some() {
            if held.is_empty() {
                self.ready_by = None;
                out.push(Signal::Ready(false));
            } else if let Some(signal) = self.take_ready(held) {
                out.push(signal);
            }
        }
        if self.play_by.is_some() && (held.is_empty() || was_front) {
            out.push(Signal::Finish);
        }
        out
    }
}

/// What runs once a play ends.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AfterPlay {
    RequestLock,
    Stop,
}

pub struct TransitionSurface {
    output: WlOutput,
    name: String,
    window: LayerWindow,
    canvas: GlCanvas,
    motion: Motion,
    plan: Option<SlashPlan>,
    configured: bool,
    snapshotted: bool,
    held: bool,
}

pub struct Transition {
    pub active: bool,
    running: bool,
    finishing: bool,
    mode: TransitionMode,
    seed: u32,
    started: Instant,
    phase: Phase,
    surfaces: Vec<TransitionSurface>,
    thunder: ThunderBurst,
    after_play: Option<AfterPlay>,
}

impl Default for Transition {
    fn default() -> Transition {
        Transition {
            active: false,
            running: false,
            finishing: false,
            mode: TransitionMode::Lock,
            seed: 0,
            started: Instant::now(),
            phase: Phase::default(),
            surfaces: Vec::new(),
            thunder: ThunderBurst::default(),
            after_play: None,
        }
    }
}

impl Transition {
    pub fn playing(&self) -> bool {
        self.phase.playing()
    }

    fn held(&self) -> Vec<bool> {
        self.surfaces.iter().map(|s| s.held).collect()
    }

    fn index_of(&self, surface: &WlSurface) -> Option<usize> {
        self.surfaces.iter().position(|s| s.window.owns(surface))
    }
}

fn new_seed() -> u32 {
    (SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_nanos() & 0xFFFF_FFFF) as u32
}

/// Draws the transition's lightning with the thunder shaders.
struct Bolts<'a> {
    burst: &'a mut ThunderBurst,
}

impl Thunder<GlFrame<'_>> for Bolts<'_> {
    fn strike(&mut self, canvas: &mut GlFrame<'_>, s: &SlashStrike) {
        let p = ThunderParams {
            ax: s.a.x,
            ay: s.a.y,
            bx: s.b.x,
            by: s.b.y,
            time_s: s.time_s,
            progress: s.progress,
            intensity: s.intensity,
            seed: s.seed,
            amp: s.amp,
            thick: s.thick,
            core: WHITE.rgba(),
            glow: palette().electro.rgba(),
            ..ThunderParams::default()
        };
        self.burst.burst_draw(canvas.renderer(), &p);
    }

    fn shock(&mut self, canvas: &mut GlFrame<'_>, s: &SlashShock) {
        let p = ThunderShockParams {
            cx: s.center.x,
            cy: s.center.y,
            radius: s.radius,
            time_s: s.time_s,
            progress: s.progress,
            intensity: s.intensity,
            core: WHITE.rgba(),
            glow: palette().electro.rgba(),
        };
        self.burst.shock_draw(canvas.renderer(), &p);
    }
}

/// The wallpaper the lock shows on `name`: the custom lock wallpaper when loaded, else the desktop one.
#[allow(clippy::too_many_arguments)]
fn draw_wallpaper(frame: &mut GlFrame, wallpaper: &mut Wallpaper, monitors: &[crate::backend::wayland::monitor::Monitor], cfg: &Config, name: &str, w: i32, h: i32, scale: i32) {
    if wallpaper.loaded() {
        wallpaper.draw(cfg, frame.renderer(), name, w, h, scale);
    } else if let Some(desktop) = monitors.iter().find(|m| m.name == name).and_then(|m| m.wallpaper.as_ref()) {
        desktop.draw_columns(frame.renderer(), w, h);
    }
}

impl State {
    pub fn lock_transition_active(&self) -> bool {
        self.lock.transition.active
    }

    fn lock_transition_open(&mut self, mode: TransitionMode) -> bool {
        if self.lock.transition.active {
            return false;
        }
        let now = Instant::now();
        let t = &mut self.lock.transition;
        t.mode = mode;
        t.seed = new_seed();
        t.running = false;
        t.finishing = false;
        t.phase.stop();
        t.after_play = None;
        if mode == TransitionMode::Unlock {
            t.phase.wait_ready(now);
        }
        for monitor in &self.monitors {
            let surface = self.compositor.create_surface(&self.qh);
            let layer = self.layer_shell.create_layer_surface(&self.qh, surface, Layer::Overlay, Some(LAYER_NAMESPACE), Some(&monitor.output));
            layer.set_anchor(Anchor::TOP | Anchor::BOTTOM | Anchor::LEFT | Anchor::RIGHT);
            layer.set_exclusive_zone(-1);
            layer.set_keyboard_interactivity(KeyboardInteractivity::None);
            set_input_region(layer.wl_surface(), &self.compositor, false);
            layer.commit();
            let mut window = LayerWindow::new(layer, 1);
            window.set_scale(monitor.scale);
            t.surfaces.push(TransitionSurface {
                output: monitor.output.clone(),
                name: monitor.name.clone(),
                window,
                canvas: GlCanvas::new(),
                motion: Motion::default(),
                plan: None,
                configured: false,
                snapshotted: false,
                held: false,
            });
        }
        if t.surfaces.is_empty() {
            t.phase.stop();
            return false;
        }
        t.active = true;
        ::log::info!("lock: transition opened ({} surface(s))", t.surfaces.len());
        true
    }

    /// Opens the lock-mode transition and plays it; the lock is requested when it ends.
    pub fn lock_transition_open_lock(&mut self) -> bool {
        if !self.lock_transition_open(TransitionMode::Lock) {
            return false;
        }
        self.lock_transition_play(AfterPlay::RequestLock)
    }

    /// Opens or reuses the unlock-mode transition; false when the unlock runs without it.
    pub fn lock_transition_open_unlock(&mut self) -> bool {
        let t = &self.lock.transition;
        let reuse = t.active && !t.playing() && !t.surfaces.is_empty();
        let animated = !self.config().animations_disabled && !self.lock.surfaces.is_empty() && (reuse || self.lock_transition_open(TransitionMode::Unlock));
        ::log::info!("lock: unlock begin (reuse={reuse} animated={animated})");
        if !animated {
            return false;
        }
        if reuse {
            self.lock_transition_rehold(TransitionMode::Unlock);
        }
        true
    }

    /// The unlock overlays hold the lock picture (or gave up waiting).
    fn lock_unlock_ready(&mut self, held: bool) {
        ::log::info!("lock: unlock overlays ready={held}");
        if !self.lock.has_session() {
            return;
        }
        if !held {
            self.lock_finish_unlock();
            return;
        }
        self.lock_release_session();
        if !self.lock_transition_play(AfterPlay::Stop) {
            self.lock_transition_stop();
        }
    }

    pub fn lock_transition_release_keyboard(&mut self) {
        let t = &mut self.lock.transition;
        if !t.active {
            return;
        }
        for surface in &t.surfaces {
            surface.window.layer.set_keyboard_interactivity(KeyboardInteractivity::None);
            surface.window.layer.commit();
        }
    }

    fn lock_transition_rehold(&mut self, mode: TransitionMode) {
        let now = Instant::now();
        let t = &mut self.lock.transition;
        t.mode = mode;
        t.phase.wait_ready(now);
        t.running = false;
        t.finishing = false;
        for surface in &mut t.surfaces {
            surface.held = false;
            surface.snapshotted = false;
        }
        for i in 0..self.lock.transition.surfaces.len() {
            let s = &self.lock.transition.surfaces[i];
            if s.configured && s.window.egl_surface.is_some() {
                self.lock_transition_hold(i);
            }
        }
        self.egl.make_current(None);
    }

    fn lock_transition_play(&mut self, after: AfterPlay) -> bool {
        let t = &mut self.lock.transition;
        if !t.active || t.playing() {
            return false;
        }
        t.after_play = Some(after);
        t.seed = new_seed();
        t.running = false;
        t.finishing = false;
        t.phase.play(Instant::now());
        let (mode, seed) = (t.mode, t.seed);
        let mut draw = Vec::new();
        for (i, surface) in t.surfaces.iter_mut().enumerate() {
            surface.window.layer.set_keyboard_interactivity(KeyboardInteractivity::Exclusive);
            if mode == TransitionMode::Lock {
                surface.snapshotted = false;
            }
            if surface.configured {
                surface.plan = Some(SlashPlan::new(seed, surface.window.width as f32, surface.window.height as f32));
                surface.window.frame.drop_callback();
                if surface.window.request_frame(&self.qh) {
                    draw.push(i);
                }
            } else {
                surface.window.layer.commit();
            }
        }
        ::log::info!("lock: transition playing");
        for i in draw {
            self.lock_transition_paint(i);
        }
        true
    }

    fn lock_transition_finish(&mut self) {
        let t = &mut self.lock.transition;
        if !t.phase.finish() {
            return;
        }
        t.finishing = false;
        match t.after_play.take() {
            Some(AfterPlay::RequestLock) => {
                if !self.lock_request() {
                    self.lock_transition_stop();
                }
            }
            Some(AfterPlay::Stop) => self.lock_transition_stop(),
            None => {}
        }
    }

    fn lock_transition_signal(&mut self, signal: Signal) {
        match signal {
            Signal::Ready(held) => self.lock_unlock_ready(held),
            Signal::Finish => self.lock_transition_finish(),
        }
    }

    /// Runs `signal` from the loop's idle queue, never inside a configure or frame handler.
    fn lock_transition_defer(&mut self, signal: Signal) {
        self.handle.insert_idle(move |state: &mut State| state.lock_transition_signal(signal));
    }

    pub fn lock_transition_check_deadline(&mut self) {
        match self.lock.transition.phase.check_deadline(Instant::now()) {
            Some(Signal::Ready(false)) => {
                ::log::info!("lock: transition overlays not ready, giving up");
                self.lock_unlock_ready(false);
            }
            Some(Signal::Finish) => {
                ::log::info!("lock: transition overdue, finishing it");
                self.lock_transition_finish();
            }
            _ => {}
        }
    }

    /// Destroys the transition surfaces and frees the lock wallpaper when nothing shows it.
    pub fn lock_transition_stop(&mut self) {
        let surfaces = std::mem::take(&mut self.lock.transition.surfaces);
        for surface in surfaces {
            self.lock_transition_destroy_surface(surface);
        }
        let t = &mut self.lock.transition;
        t.active = false;
        t.running = false;
        t.finishing = false;
        t.phase.stop();
        t.after_play = None;
        self.lock_release_wallpaper_if_idle();
    }

    fn lock_transition_destroy_surface(&mut self, surface: TransitionSurface) {
        let TransitionSurface { window, mut canvas, .. } = surface;
        self.egl.make_current(None);
        canvas.release_snapshot(&self.renderer);
        canvas.destroy(&self.renderer);
        self.forget_surfaces(&[window.layer.wl_surface().id()]);
        window.destroy(&self.egl);
    }

    pub fn lock_transition_output_removed(&mut self, output: &WlOutput) {
        let t = &mut self.lock.transition;
        if !t.active {
            return;
        }
        let Some(i) = t.surfaces.iter().position(|s| s.output == *output) else { return };
        let surface = t.surfaces.remove(i);
        let signals = t.phase.removed(&t.held(), i == 0);
        self.lock_transition_destroy_surface(surface);
        for signal in signals {
            self.lock_transition_defer(signal);
        }
    }

    pub fn lock_transition_request_all(&mut self) {
        let mut draw = Vec::new();
        for (i, surface) in self.lock.transition.surfaces.iter_mut().enumerate() {
            if surface.configured && surface.window.request_frame(&self.qh) {
                draw.push(i);
            }
        }
        for i in draw {
            self.lock_transition_paint(i);
        }
    }

    /// A frame callback on a transition surface; true when it was one.
    pub fn lock_transition_frame_done(&mut self, surface: &WlSurface) -> bool {
        let Some(i) = self.lock.transition.index_of(surface) else { return false };
        if self.lock.transition.surfaces[i].window.frame_done(&self.qh) {
            self.lock_transition_paint(i);
        }
        true
    }

    pub fn lock_transition_scale_changed(&mut self, surface: &WlSurface, scale: i32) -> bool {
        let Some(i) = self.lock.transition.index_of(surface) else { return false };
        self.lock.transition.surfaces[i].window.set_scale(scale);
        if self.lock.transition.surfaces[i].window.request_frame(&self.qh) {
            self.lock_transition_paint(i);
        }
        true
    }

    /// A layer configure for a transition surface; true when it was one.
    pub fn lock_transition_configure(&mut self, layer: &LayerSurface, size: (u32, u32)) -> bool {
        let Some(i) = self.lock.transition.index_of(layer.wl_surface()) else { return false };
        let t = &mut self.lock.transition;
        let seed = t.seed;
        let hold = t.mode == TransitionMode::Unlock && !t.playing();
        let surface = &mut t.surfaces[i];
        surface.window.height = size.1 as i32;
        surface.window.configure(&self.egl, size.0 as i32);
        if surface.window.egl_surface.is_none() {
            ::log::error!("lock: transition eglCreateWindowSurface failed on '{}'", surface.name);
            return true;
        }
        surface.plan = Some(SlashPlan::new(seed, surface.window.width as f32, surface.window.height as f32));
        surface.snapshotted = false;
        surface.held = false;
        surface.configured = true;
        if hold {
            self.lock_transition_hold(i);
        } else if surface.window.request_frame(&self.qh) {
            self.lock_transition_paint(i);
        }
        self.egl.make_current(None);
        true
    }

    /// Paints the lock card once and keeps it as the snapshot the unlock cuts.
    fn lock_transition_hold(&mut self, i: usize) {
        let cfg = self.config();
        let unlocking = self.lock.unlocking;
        let lock = &mut self.lock;
        let Some(surface) = lock.transition.surfaces.get_mut(i) else { return };
        if !surface.window.begin(&self.egl, &mut self.renderer) {
            return;
        }
        let (w, h, scale) = (surface.window.width, surface.window.height, surface.window.scale);
        {
            let mut frame = surface.canvas.begin(&mut self.renderer, scale);
            draw_wallpaper(&mut frame, &mut lock.wallpaper, &self.monitors, &cfg, &surface.name, w, h, scale);
            let motion = if unlocking { lock.surfaces.iter_mut().find(|s| s.name == surface.name).map(|s| &mut s.motion) } else { None };
            let motion = motion.unwrap_or(&mut surface.motion);
            draw_card(&mut frame, &mut lock.model, motion, self.core.services.as_ref(), &lock.user, &cfg, &surface.name, w, h);
            use crate::render::canvas::Canvas;
            frame.snapshot();
        }
        if !surface.window.end(&self.egl) {
            ::log::error!("lock: transition hold eglSwapBuffers failed on '{}'", surface.name);
            return;
        }
        surface.snapshotted = true;
        surface.held = true;
        let held = lock.transition.held();
        if let Some(signal) = lock.transition.phase.take_ready(&held) {
            self.lock_transition_defer(signal);
        }
    }

    fn lock_transition_paint(&mut self, i: usize) {
        let cfg = self.config();
        let unlocking = self.lock.unlocking;
        let lock = &mut self.lock;
        let t = &mut lock.transition;
        if !t.playing() {
            return;
        }
        let Some(surface) = t.surfaces.get_mut(i) else { return };
        if !surface.configured || surface.plan.is_none() {
            return;
        }
        if !t.running {
            t.running = true;
            t.started = Instant::now();
            ::log::info!("lock: transition first frame ({})", if t.mode == TransitionMode::Unlock { "unlock" } else { "lock" });
        }
        let elapsed = Instant::now().saturating_duration_since(t.started).as_secs_f32() * 1000.0;
        let progress = slash_run_progress(elapsed);
        if !surface.window.begin(&self.egl, &mut self.renderer) {
            return;
        }
        let (w, h, scale) = (surface.window.width, surface.window.height, surface.window.scale);
        let mode = t.mode;
        let name = surface.name.clone();
        let mut frame = surface.canvas.begin(&mut self.renderer, scale);
        if !surface.snapshotted {
            if mode == TransitionMode::Unlock {
                draw_wallpaper(&mut frame, &mut lock.wallpaper, &self.monitors, &cfg, &name, w, h, scale);
                let motion = lock.surfaces.iter_mut().find(|s| s.name == name).map(|s| &mut s.motion).unwrap_or(&mut surface.motion);
                draw_card(&mut frame, &mut lock.model, motion, self.core.services.as_ref(), &lock.user, &cfg, &name, w, h);
            } else {
                draw_wallpaper(&mut frame, &mut lock.wallpaper, &self.monitors, &cfg, &name, w, h, scale);
            }
            use crate::render::canvas::Canvas;
            frame.snapshot();
            frame.renderer().clear();
            let pending = mode == TransitionMode::Lock && progress < cfg::SLASH_FINAL_START && !lock.wallpaper.ready(&name);
            surface.snapshotted = !pending;
        }
        let services = self.core.services.as_ref();
        let (model, user, wallpaper, monitors) = (&mut lock.model, lock.user.as_str(), &mut lock.wallpaper, &self.monitors);
        let lock_motion = if unlocking { lock.surfaces.iter_mut().find(|s| s.name == name).map(|s| &mut s.motion) } else { None };
        let motion = lock_motion.unwrap_or(&mut surface.motion);
        let mut base = |f: &mut GlFrame| {
            draw_wallpaper(f, wallpaper, monitors, &cfg, &name, w, h, scale);
            draw_card(f, model, motion, services, user, &cfg, &name, w, h);
        };
        let Some(plan) = surface.plan.as_ref() else { return };
        let mut bolts = Bolts { burst: &mut t.thunder };
        paint_transition(&mut frame, plan, progress, Some(&mut base), Some(&mut bolts), mode);
        frame.flush();
        surface.window.end(&self.egl);
        if elapsed < cfg::SLASH_MS {
            surface.window.request_frame(&self.qh);
        } else if !t.finishing && i == 0 {
            t.finishing = true;
            self.lock_transition_defer(Signal::Finish);
        }
        self.egl.make_current(None);
    }
}

/// The lock card over the wallpaper, unless the card is off on `name`.
#[allow(clippy::too_many_arguments)]
fn draw_card(frame: &mut GlFrame, model: &mut Model, motion: &mut Motion, services: Option<&crate::service::Services>, user: &str, cfg: &Config, name: &str, w: i32, h: i32) {
    if !lock_enabled_for(cfg, name) {
        return;
    }
    let oh = h as f32;
    motion_fit(motion, card_width(oh), card_height(oh));
    with_info(services, user, |info| view::paint(frame, model, motion, info, w as f32, oh));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn transition_ready_deadline() {
        let t0 = Instant::now();
        let mut phase = Phase::default();
        assert_eq!(phase.take_ready(&[true, true]), None, "nothing waits yet");
        phase.wait_ready(t0);
        assert_eq!(phase.take_ready(&[true, false]), None, "one surface still unheld");
        assert_eq!(phase.take_ready(&[true, true]), Some(Signal::Ready(true)), "every surface holds");
        assert_eq!(phase.take_ready(&[true, true]), None, "at most once");
        assert_eq!(phase.check_deadline(t0 + Duration::from_secs(5)), None, "nothing left to time out");

        phase.wait_ready(t0);
        assert_eq!(phase.check_deadline(t0 + Duration::from_millis(TRANSITION_READY_MS / 2)), None, "before the deadline");
        assert_eq!(phase.check_deadline(t0 + Duration::from_millis(TRANSITION_READY_MS + 1)), Some(Signal::Ready(false)), "the deadline gives up");
        assert_eq!(phase.take_ready(&[true]), None, "and only once");

        phase.play(t0);
        assert!(phase.playing());
        let overdue = t0 + Duration::from_millis(cfg::SLASH_MS as u64 + TRANSITION_DEADLINE_MARGIN_MS + 1);
        assert_eq!(phase.check_deadline(t0 + Duration::from_millis(cfg::SLASH_MS as u64)), None, "still within the play deadline");
        assert_eq!(phase.check_deadline(overdue), Some(Signal::Finish), "an overdue play finishes");
        assert!(phase.finish() && !phase.finish(), "the host finishes once");
    }

    #[test]
    fn transition_output_removed() {
        let t0 = Instant::now();
        let mut phase = Phase::default();
        phase.wait_ready(t0);
        assert_eq!(phase.removed(&[true], false), vec![Signal::Ready(true)], "removing the last unheld surface resolves ready");

        phase.wait_ready(t0);
        assert_eq!(phase.removed(&[], true), vec![Signal::Ready(false)], "removing every surface gives up");

        phase.play(t0);
        assert_eq!(phase.removed(&[false, false], false), vec![], "a back surface going away keeps playing");
        assert_eq!(phase.removed(&[false], true), vec![Signal::Finish], "the front surface going away finishes");
        assert!(phase.finish());
        phase.play(t0);
        assert_eq!(phase.removed(&[], false), vec![Signal::Finish], "the last surface going away finishes");
    }
}
