//! The launcher overlay surface: the card, its lightning entrance, and the field the input method types into.

use std::cell::Cell;
use std::rc::Rc;
use std::time::{Instant, SystemTime, UNIX_EPOCH};

use smithay_client_toolkit::compositor::CompositorState;
use wayland_client::QueueHandle;
use wayland_client::protocol::wl_surface::WlSurface;

use crate::wayland::State;
use crate::wayland::render::egl::Egl;
use crate::wayland::render::gl_canvas::GlCanvas;
use crate::wayland::render::overlay::{OVERLAY_FADE_OWNER, Overlay};
use crate::wayland::render::renderer::Renderer;
use crate::wayland::render::thunder::{ThunderBurst, ThunderParams, ThunderRectShockParams};
use crate::wayland::text_input::{EditTarget, TextInputPurpose, TextInputState};
use crate::base::animation::{Easing, Value, value, writer};
use crate::base::input::{Clipboard, KeyEvent};
use crate::config::launcher_config as cfg;
use crate::modules::launcher::entrance::EntranceState;
use crate::modules::launcher::model::Model;
use crate::modules::launcher::view::{self, Frame, field_column, field_hit};
use crate::render::canvas::Canvas;
use crate::render::theme::palette;
use crate::render::tokens::{WHITE, metrics};

pub const LAYER_NAMESPACE: &str = "astralia-shell-launcher";

const ENTRANCE_OWNER: u64 = 2;
const ENTRANCE_BOLT_GAIN: f32 = 1.3;

impl EditTarget for Model {
    fn delete_before(&mut self, n: u32) {
        Model::delete_before(self, n);
    }
    fn commit_text(&mut self, text: &str) {
        Model::commit_text(self, text);
    }
    fn set_preedit(&mut self, text: &str) {
        Model::set_preedit(self, text);
    }
}

pub struct Launcher {
    pub overlay: Overlay,
    canvas: GlCanvas,
    pub model: Model,
    thunder: ThunderBurst,
    frame: Frame,
    lightning: bool,
    entrance_t: Value,
    entrance_seed: f32,
    entrance_active: Rc<Cell<bool>>,
    /// Fading out; the model closes when the fade ends.
    closing: bool,
    /// Whether the input method serves the field now.
    text_focused: bool,
    /// Closed; the host drops the field and destroys the surface.
    surface_due: bool,
}

impl Launcher {
    /// `wake` runs when a search, index build, paste or icon finishes, so the host polls and repaints.
    pub fn new(wake: Rc<dyn Fn()>, clipboard: Option<Rc<dyn Clipboard>>) -> Launcher {
        let mut model = Model::default();
        model.set_wake(wake.clone());
        model.set_clipboard(clipboard);
        let mut canvas = GlCanvas::new();
        canvas.set_on_image_ready(wake);
        Launcher {
            overlay: Overlay::new(LAYER_NAMESPACE),
            canvas,
            model,
            thunder: ThunderBurst::default(),
            frame: Frame::default(),
            lightning: false,
            entrance_t: value(1.0),
            entrance_seed: 0.0,
            entrance_active: Rc::default(),
            closing: false,
            text_focused: false,
            surface_due: false,
        }
    }

    pub fn is_open(&self) -> bool {
        self.overlay.is_open()
    }

    /// Opens searching from home (or `/` when `global`), with the lightning entrance unless animations are off;
    /// or fades out.
    pub fn toggle(&mut self, compositor: &CompositorState, global: bool, lightning: bool) {
        if self.overlay.is_open() {
            self.close(compositor);
            return;
        }
        ::log::info!("launcher: OPEN (global={})", u8::from(global));
        if self.model.is_open() {
            self.model.close();
        }
        self.model.open(global);
        self.lightning = lightning;
        self.closing = false;
        self.overlay.toggle(compositor, !lightning);
        // Before the first configure the open waits in the overlay; `configure` starts the entrance then.
        if self.overlay.is_open() && lightning {
            self.start_entrance();
        }
    }

    fn close(&mut self, compositor: &CompositorState) {
        if !self.overlay.is_open() || self.closing {
            return;
        }
        ::log::info!("launcher: CLOSE");
        self.model.clear_preedit();
        self.stop_entrance();
        self.closing = true;
        self.overlay.toggle(compositor, true);
    }

    /// Closes at once, without the fade.
    pub fn fast_close(&mut self, compositor: &CompositorState) {
        if !self.overlay.is_open() {
            return;
        }
        self.overlay.release(compositor);
        self.finish_close();
    }

    fn finish_close(&mut self) {
        self.stop_entrance();
        self.model.close();
        self.closing = false;
        self.surface_due = true;
    }

    /// True once after a close finished, so the host destroys the surface.
    pub fn take_surface_due(&mut self) -> bool {
        std::mem::take(&mut self.surface_due)
    }

    fn start_entrance(&mut self) {
        let state = &mut self.overlay.state;
        state.opacity.set(1.0);
        state.animations.cancel_owner(OVERLAY_FADE_OWNER);
        self.entrance_active.set(true);
        self.entrance_t.set(0.0);
        let nanos = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().subsec_nanos();
        self.entrance_seed = (nanos % 4096) as f32 / 64.0;
        let active = self.entrance_active.clone();
        let done = Box::new(move |_: &mut _| active.set(false));
        state.animations.animate(0.0, 1.0, cfg::ENTRANCE_MS, Easing::Linear, writer(&self.entrance_t), Some(done), ENTRANCE_OWNER);
    }

    fn stop_entrance(&mut self) {
        self.overlay.state.animations.cancel_owner(ENTRANCE_OWNER);
        self.entrance_active.set(false);
    }

    pub fn configure(&mut self, egl: &Egl, compositor: &CompositorState, width: i32, height: i32) {
        if self.overlay.configure(egl, compositor, width, height) && self.lightning {
            self.start_entrance();
        }
    }

    /// Applies finished searches and pastes; true when any arrived.
    pub fn poll(&mut self) -> bool {
        self.model.poll()
    }

    pub fn key(&mut self, compositor: &CompositorState, event: &KeyEvent) {
        if !self.overlay.is_open() || self.closing {
            return;
        }
        self.model.key(event);
        if self.model.take_close_request() {
            self.close(compositor);
        }
    }

    fn row_at(&self, x: f32, y: f32) -> Option<usize> {
        self.frame.rows.iter().find(|(r, _)| x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h).map(|&(_, i)| i)
    }

    /// A left press: a row launches, the field places the caret, outside the card closes.
    pub fn press(&mut self, compositor: &CompositorState, x: f32, y: f32) {
        if !self.overlay.is_open() || self.closing {
            return;
        }
        if let Some(row) = self.row_at(x, y) {
            self.model.click_row(row);
        } else if field_hit(&self.frame, x, y) {
            self.model.pointer_down(field_column(&self.frame, x), Instant::now());
        } else {
            let r = self.frame.rect;
            if !(x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
                self.close(compositor);
            }
        }
        if self.model.take_close_request() {
            self.close(compositor);
        }
    }

    pub fn release(&mut self) {
        self.model.pointer_up();
    }

    /// The pointer over the overlay (`Some`) or gone (`None`); true when the card changed.
    pub fn pointer(&mut self, at: Option<(f32, f32)>) -> bool {
        let open = self.overlay.is_open();
        let row = at.filter(|_| open).and_then(|(x, y)| self.row_at(x, y));
        let hover = self.model.hover(row);
        let drag = match at {
            Some((x, _)) if open && self.model.dragging() => {
                self.model.pointer_drag(field_column(&self.frame, x));
                true
            }
            _ => false,
        };
        hover || drag
    }

    pub fn wants_hand(&self) -> bool {
        self.overlay.is_open() && self.model.hovered().is_some()
    }

    /// The caret blink; true when the overlay repaints for it.
    pub fn blink(&mut self) -> bool {
        if !self.overlay.is_open() {
            return false;
        }
        self.model.toggle_caret();
        true
    }

    pub fn text_input_state(&self) -> TextInputState {
        let c = self.frame.caret;
        TextInputState { cursor_rect_x: c.x as i32, cursor_rect_y: c.y as i32, cursor_rect_w: c.w as i32, cursor_rect_h: c.h as i32, purpose: TextInputPurpose::Normal }
    }

    /// When the field should gain (`Some(true)`) or lose (`Some(false)`) the input method, with its surface.
    pub fn text_input_change(&mut self) -> Option<(bool, WlSurface)> {
        let want = self.overlay.is_open() && !self.closing;
        if want == self.text_focused {
            return None;
        }
        // After an unplug the surface is gone and the caller has already dropped the field.
        self.text_focused = want;
        self.overlay.surface().map(|surface| (want, surface))
    }

    /// Asks for a frame while open or fading out; true when the caller draws now.
    pub fn request_frame(&mut self, qh: &QueueHandle<State>) -> bool {
        let wanted = self.overlay.is_open() || self.closing;
        wanted && self.overlay.window_mut().is_some_and(|w| w.request_frame(qh))
    }

    /// Paints one frame; true while something animates, so the caller asks for another.
    pub fn draw(&mut self, egl: &Egl, renderer: &mut Renderer, compositor: &CompositorState) -> bool {
        let now = self.overlay.frame_time();
        self.overlay.tick(compositor, now);
        if !self.overlay.is_open() {
            if self.closing {
                self.finish_close();
            }
            return false;
        }
        self.model.tick(now);
        self.model.sync_layout();
        let opacity = self.overlay.opacity();
        let (width, height) = (self.overlay.width() as f32, self.overlay.height() as f32);
        let Some(window) = self.overlay.window_mut() else { return false };
        if !window.begin(egl, renderer) {
            return false;
        }
        let scale = window.scale;
        let mut frame = self.canvas.begin(renderer, scale);
        frame.set_opacity(opacity);
        let entering = self.entrance_active.get();
        let entrance = EntranceState { t: self.entrance_t.get(), seed: self.entrance_seed };
        self.frame = view::paint(&mut frame, &mut self.model, width, height, entering.then_some(&entrance));
        frame.flush();
        if entering {
            self.draw_entrance(renderer);
        }
        if let Some(window) = self.overlay.window_mut() {
            window.end(egl);
        }
        self.overlay.state.animations.has_active() || self.model.animating()
    }

    fn draw_entrance(&mut self, renderer: &Renderer) {
        let e = &self.frame.entrance;
        let (core, glow) = (WHITE.rgba(), palette().electro.rgba());
        for burst in &e.bursts {
            let r = burst.rect;
            let shock = ThunderRectShockParams {
                cx: r.x + r.w / 2.0,
                cy: r.y + r.h / 2.0,
                half_w: r.w / 2.0,
                half_h: r.h / 2.0,
                corner: metrics::RADIUS_CARD,
                reach: burst.reach,
                time_s: e.time_s,
                progress: burst.progress,
                intensity: burst.intensity * ENTRANCE_BOLT_GAIN,
                core,
                glow,
            };
            self.thunder.rect_shock_draw(renderer, &shock);
        }
        for bolt in &e.bolts {
            let p = ThunderParams {
                ax: bolt.ax,
                ay: bolt.ay,
                bx: bolt.bx,
                by: bolt.by,
                time_s: e.time_s,
                progress: bolt.progress,
                intensity: bolt.intensity * ENTRANCE_BOLT_GAIN,
                seed: bolt.seed,
                amp: bolt.amp,
                thick: bolt.thick,
                stitch: bolt.stitch,
                core,
                glow,
                ..ThunderParams::default()
            };
            self.thunder.burst_draw(renderer, &p);
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
