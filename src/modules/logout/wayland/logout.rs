//! The logout overlay surface: the ring, the logo and the lightning drawn over it.

use std::cell::Cell;
use std::rc::Rc;
use std::time::Instant;

use smithay_client_toolkit::compositor::CompositorState;

use crate::wayland::render::egl::Egl;
use crate::wayland::render::gl_canvas::GlCanvas;
use crate::wayland::render::overlay::Overlay;
use crate::wayland::render::renderer::Renderer;
use crate::wayland::render::thunder::{ThunderBurst, ThunderParams, ThunderShockParams};
use crate::base::input::KeyEvent;
use crate::base::process::spawn_detached;
use crate::config::logout_config as cfg;
use crate::modules::logout::layout::{Point, button_center};
use crate::modules::logout::model::{COUNT, Model, commands};
use crate::modules::logout::view;
use crate::render::canvas::Canvas;
use crate::render::geometry::Rect;
use crate::render::theme::palette;
use crate::render::tokens::WHITE;

pub const LAYER_NAMESPACE: &str = "astralia-shell-logout";

const BUTTONS_RADIUS: f32 = cfg::RING_RADIUS as f32;
const SLASH_OVERSHOOT: f32 = 0.3;
const FINISH_SPAN: f32 = BUTTONS_RADIUS * 5.2;
const FINISH_RISE: f32 = 230.0;
const FINISH_THICK: f32 = 3.4;
const FINISH_SWEEP: f32 = 2.5;
const FINISH_INTENSITY: f32 = 2.8;
const FINISH_LINGER_INTENSITY: f32 = 1.4;
const BURST_RING_MAX: f32 = BUTTONS_RADIUS * 1.5;
const BOLT_AMP: f32 = 13.0;

/// Button `index`'s square around the ring centred on `(center_x, center_y)`.
pub fn button_rect(index: usize, center_x: f32, center_y: f32) -> Rect {
    let c = button_center(index, Point { x: f64::from(center_x), y: f64::from(center_y) });
    let size = cfg::BUTTON_SIZE as f32;
    Rect { x: c.x as f32 - size / 2.0, y: c.y as f32 - size / 2.0, w: size, h: size }
}

/// The ring vertex the `step`th slash starts from: the slashes trace a star.
fn star_vertex(step: usize) -> usize {
    step * cfg::STAR_STEP as usize % COUNT
}

pub struct Logout {
    pub overlay: Overlay,
    canvas: GlCanvas,
    model: Model,
    thunder: ThunderBurst,
    logo_path: String,
    /// Set by the model when its exit ends; the host then gives the input back.
    closed: Rc<Cell<bool>>,
    /// A closed overlay still shows its last frame until it draws once more.
    stale: bool,
    pub opened_by_widget: bool,
    /// The output it opened on.
    output: String,
}

impl Logout {
    pub fn new() -> Logout {
        let mut model = Model::new(commands());
        let closed = Rc::new(Cell::new(false));
        let flag = closed.clone();
        model.on_execute = Some(Box::new(|command: &str| spawn_detached(command)));
        model.on_closed = Some(Box::new(move || flag.set(true)));
        Logout { overlay: Overlay::new(LAYER_NAMESPACE), canvas: GlCanvas::new(), model, thunder: ThunderBurst::default(), logo_path: String::new(), closed, stale: false, opened_by_widget: false, output: String::new() }
    }

    pub fn is_open(&self) -> bool {
        self.overlay.is_open()
    }

    /// Open on `output` from the bar's logout item.
    pub fn shown_on(&self, output: &str) -> bool {
        self.is_open() && self.opened_by_widget && self.output == output
    }

    /// Opens on `output` with the whole choreography, or plays the exit.
    pub fn toggle(&mut self, compositor: &CompositorState, logo_path: &str, output: &str, by_widget: bool) {
        self.logo_path = logo_path.to_string();
        if !self.overlay.is_open() {
            if self.overlay.toggle(compositor, false) {
                ::log::info!("logout: toggle open=1 by_widget={}", u8::from(by_widget));
                self.opened_by_widget = by_widget;
                self.output = output.to_string();
                // Before the first configure the open waits in the overlay; `configure` starts the model then.
                if self.overlay.is_open() {
                    self.model.open();
                }
            }
            return;
        }
        ::log::info!("logout: toggle open=0 by_widget={}", u8::from(by_widget));
        self.model.toggle();
        self.settle(compositor);
    }

    /// Closes at once, without the exit.
    pub fn fast_close(&mut self, compositor: &CompositorState) {
        if self.overlay.is_open() {
            self.model.fast_hide();
            self.settle(compositor);
        }
    }

    /// Gives the input back once the model has closed.
    fn settle(&mut self, compositor: &CompositorState) {
        if self.closed.replace(false) {
            self.opened_by_widget = false;
            self.stale = true;
            self.overlay.release(compositor);
        }
    }

    pub fn configure(&mut self, egl: &Egl, compositor: &CompositorState, width: i32, height: i32) {
        if self.overlay.configure(egl, compositor, width, height) {
            self.model.open();
        }
    }

    pub fn key(&mut self, compositor: &CompositorState, event: &KeyEvent) {
        self.model.key(event);
        self.settle(compositor);
    }

    pub fn click(&mut self, compositor: &CompositorState, x: f64, y: f64) {
        self.model.click(x, y, f64::from(self.overlay.width()), f64::from(self.overlay.height()));
        self.settle(compositor);
    }

    /// The pointer over the overlay (`Some`) or gone (`None`); true when the hover changed.
    pub fn pointer(&mut self, at: Option<(f64, f64)>) -> bool {
        if !self.overlay.is_open() {
            return false;
        }
        match at {
            Some((x, y)) => self.model.hover(x, y, f64::from(self.overlay.width()), f64::from(self.overlay.height())),
            None => self.model.clear_hover(),
        }
    }

    /// Asks for a frame while open, and once more after closing to clear the surface; true when the caller draws now.
    pub fn request_frame(&mut self, qh: &wayland_client::QueueHandle<crate::wayland::State>) -> bool {
        let wanted = self.overlay.is_open() || self.stale;
        wanted && self.overlay.window_mut().is_some_and(|w| w.request_frame(qh))
    }

    pub fn wants_hand(&self) -> bool {
        self.overlay.is_open() && self.model.hovered().is_some()
    }

    /// Paints one frame; true while the choreography runs, so the caller asks for another.
    pub fn draw(&mut self, egl: &Egl, renderer: &mut Renderer, compositor: &CompositorState) -> bool {
        let now = Instant::now();
        self.model.tick(now);
        self.settle(compositor);
        let opacity = self.overlay.opacity();
        let open = self.overlay.is_open();
        self.stale = false;
        let (width, height) = (self.overlay.width() as f32, self.overlay.height() as f32);
        let Some(window) = self.overlay.window_mut() else { return false };
        if !window.begin(egl, renderer) {
            return false;
        }
        let scale = window.scale;
        let mut frame = self.canvas.begin(renderer, scale);
        frame.set_opacity(opacity);
        if open {
            view::paint(&mut frame, &self.model, width, height, &self.logo_path);
        }
        frame.flush();
        if open {
            self.draw_lightning(renderer, now, width, height);
        }
        if let Some(window) = self.overlay.window_mut() {
            window.end(egl);
        }
        self.model.animating()
    }

    fn draw_lightning(&mut self, renderer: &Renderer, now: Instant, width: f32, height: f32) {
        let (cx, cy) = (width / 2.0, height / 2.0);
        let time_s = now.saturating_duration_since(self.model.burst_started()).as_secs_f32();
        let p = palette();
        let (core, glow) = (WHITE.rgba(), p.electro.rgba());
        let mut bolt = ThunderParams { time_s, core, glow, amp: BOLT_AMP, ..ThunderParams::default() };
        for e in 0..COUNT {
            let s = self.model.slash(e);
            if s <= 0.002 {
                continue;
            }
            let (va, vb) = (button_rect(star_vertex(e), cx, cy), button_rect(star_vertex(e + 1), cx, cy));
            let (ax, ay, bx, by) = (va.x + va.w / 2.0, va.y + va.h / 2.0, vb.x + vb.w / 2.0, vb.y + vb.h / 2.0);
            let (ox, oy) = ((bx - ax) * SLASH_OVERSHOOT, (by - ay) * SLASH_OVERSHOOT);
            bolt.ax = ax - ox;
            bolt.ay = ay - oy;
            bolt.bx = bx + ox;
            bolt.by = by + oy;
            bolt.seed = e as f32 * 1.7 + 1.0;
            bolt.progress = (s * 2.0).min(1.0);
            bolt.intensity = 4.0 * s * (1.0 - s) * 1.2;
            self.thunder.burst_draw(renderer, &bolt);
        }

        let burst = self.model.burst();
        if burst <= 0.002 || burst >= 0.999 {
            return;
        }
        let shock = ThunderShockParams { cx, cy, radius: BURST_RING_MAX, time_s, progress: burst, intensity: 1.3, core, glow };
        self.thunder.shock_draw(renderer, &shock);
        bolt.ax = cx - FINISH_SPAN / 2.0;
        bolt.ay = cy + FINISH_RISE / 2.0;
        bolt.bx = cx + FINISH_SPAN / 2.0;
        bolt.by = cy - FINISH_RISE / 2.0;
        let sweep = burst * FINISH_SWEEP;
        bolt.progress = sweep.min(1.0);
        bolt.intensity = (1.0 - burst) * FINISH_INTENSITY;
        bolt.seed = 21.0;
        bolt.amp = BOLT_AMP * 2.4;
        bolt.thick = FINISH_THICK;
        self.thunder.burst_draw(renderer, &bolt);
        if sweep >= 1.0 {
            let head_end = 1.0 / FINISH_SWEEP;
            let linger = (1.0 - (burst - head_end) / (1.0 - head_end)).max(0.0);
            let crackle = 0.55 + 0.45 * (time_s * 71.0).sin() * (time_s * 127.0).sin();
            bolt.progress = 1.0;
            bolt.intensity = linger * linger * FINISH_LINGER_INTENSITY * crackle;
            bolt.seed = 53.0;
            bolt.amp = BOLT_AMP * 1.5;
            bolt.thick = FINISH_THICK * 0.55;
            self.thunder.burst_draw(renderer, &bolt);
        }
    }

    pub fn destroy(mut self, egl: &Egl, renderer: &Renderer) {
        if egl.make_current(None) {
            self.thunder.destroy(renderer);
            self.canvas.destroy(renderer);
        }
        self.overlay.destroy(egl);
    }
}
