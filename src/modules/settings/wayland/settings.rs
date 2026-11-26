//! The settings overlay surface: the card, its edits written to `config.json`, and the field the input method types into.

use crate::wayland::render::wallpaper_column::DecodeStatus;
use std::cell::RefCell;
use std::rc::Rc;
use std::time::Instant;

use smithay_client_toolkit::compositor::CompositorState;
use wayland_client::QueueHandle;
use wayland_client::protocol::wl_surface::WlSurface;

use crate::wayland::State;
use crate::wayland::render::egl::Egl;
use crate::wayland::render::gl_canvas::GlCanvas;
use crate::wayland::render::overlay::Overlay;
use crate::wayland::render::renderer::Renderer;
use crate::wayland::text_input::{EditTarget, TextInputPurpose, TextInputState};
use crate::base::input::KeyEvent;
use crate::config::settings_config as cfg;
use crate::modules::settings::model::{Field, Hooks, Model};
use crate::modules::settings::view::{self, Art, Frame};
use crate::render::canvas::Canvas;
use crate::service::settings::ConfigStore;
use crate::service::user;

pub const LAYER_NAMESPACE: &str = "astralia-shell-settings";

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

/// The store edits go through, the outputs as they come and go, and the compositor's focused monitor.
/// Each monitor's wallpaper column decode statuses, refreshed by the host.
pub type DecodeStatuses = Rc<RefCell<std::collections::HashMap<String, Vec<DecodeStatus>>>>;

pub fn hooks(store: Rc<dyn ConfigStore>, outputs: Rc<RefCell<Vec<String>>>, focused_monitor: Box<dyn Fn() -> String>, statuses: DecodeStatuses) -> Hooks {
    Hooks {
        store,
        monitors: Box::new(move || outputs.borrow().clone()),
        focused_monitor,
        cpu_fallback: Box::new(move |monitor, column| {
            statuses.borrow().get(monitor).and_then(|s| s.get(usize::try_from(column).ok()?).copied()) == Some(DecodeStatus::CpuFallback)
        }),
    }
}

pub struct Settings {
    pub overlay: Overlay,
    canvas: GlCanvas,
    pub model: Model,
    art: Art,
    frame: Frame,
    /// A closed overlay still shows its last frame until it draws once more.
    stale: bool,
    /// Closed: the next frame drops the thumbnails.
    release_thumbnails: bool,
    hover_clickable: bool,
    /// Whether the input method serves the focused field now.
    text_focused: bool,
}

impl Settings {
    /// `wake` runs when a scan lands or a thumbnail decodes, so the host polls and repaints.
    pub fn new(hooks: Hooks, wake: Rc<dyn Fn()>) -> Settings {
        let mut model = Model::new(hooks);
        model.set_wake(wake.clone());
        let mut canvas = GlCanvas::new();
        canvas.set_on_image_ready(wake);
        let art = Art { user_name: user::user_name(), ..Art::default() };
        Settings { overlay: Overlay::new(LAYER_NAMESPACE), canvas, model, art, frame: Frame::default(), stale: false, release_thumbnails: false, hover_clickable: false, text_focused: false }
    }

    pub fn is_open(&self) -> bool {
        self.overlay.is_open()
    }

    /// Opens on the tile menu, or plays the close; the overlay must be bound first.
    pub fn toggle(&mut self, wl: &CompositorState) {
        if self.overlay.is_open() {
            self.model.close();
            self.settle(wl);
            return;
        }
        // Settings whose output went away are still open in the model.
        self.model.fast_close();
        self.model.take_closed();
        self.art.face = user::face_path();
        self.model.open();
        if !self.overlay.toggle(wl, false) {
            self.model.fast_close();
            self.model.take_closed();
        }
    }

    /// Closes at once, without the sequence.
    pub fn fast_close(&mut self, wl: &CompositorState) {
        if self.overlay.is_open() {
            self.model.fast_close();
            self.settle(wl);
        }
    }

    /// Acts on a close the model asked for, and gives the input back once it has closed.
    fn settle(&mut self, wl: &CompositorState) {
        if self.model.take_close_request() {
            self.model.close();
        }
        if self.model.take_closed() {
            self.hover_clickable = false;
            self.stale = true;
            self.release_thumbnails = true;
            self.overlay.release(wl);
        }
    }

    pub fn configure(&mut self, egl: &Egl, wl: &CompositorState, width: i32, height: i32) {
        self.overlay.configure(egl, wl, width, height);
    }

    /// Applies finished scans.
    pub fn poll(&mut self) {
        self.model.poll();
    }

    pub fn key(&mut self, wl: &CompositorState, event: &KeyEvent) {
        if self.overlay.is_open() {
            self.model.key(event);
            self.settle(wl);
        }
    }

    pub fn press(&mut self, wl: &CompositorState, x: f64, y: f64) {
        self.model.click(x, y);
        self.settle(wl);
    }

    /// Wheel motion in surface units.
    pub fn wheel(&mut self, dy: f64) {
        self.model.scroll(dy as f32 * cfg::SCROLL_SPEED);
    }

    /// The pointer over the overlay (`Some`) or gone (`None`).
    pub fn pointer(&mut self, at: Option<(f64, f64)>) {
        self.hover_clickable = at.is_some_and(|(x, y)| self.overlay.is_open() && self.model.clickable(x, y));
    }

    pub fn wants_hand(&self) -> bool {
        self.hover_clickable
    }

    /// The once-a-second tick: the caret blinks and the uptime moves; true when the overlay repaints.
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

    /// When a field should gain (`Some(true)`) or lose (`Some(false)`) the input method, with its surface.
    pub fn text_input_change(&mut self) -> Option<(bool, WlSurface)> {
        let want = self.overlay.is_open() && self.model.focused() != Field::None;
        if want == self.text_focused {
            return None;
        }
        // After an unplug the surface is gone and the caller has already dropped the field.
        self.text_focused = want;
        self.overlay.surface().map(|surface| (want, surface))
    }

    /// Asks for a frame while open, and once more after closing to clear the surface; true when the caller draws now.
    pub fn request_frame(&mut self, qh: &QueueHandle<State>) -> bool {
        let wanted = self.overlay.is_open() || self.stale;
        wanted && self.overlay.window_mut().is_some_and(|w| w.request_frame(qh))
    }

    /// Paints one frame; true while a tween or an image fade runs, so the caller asks for another.
    pub fn draw(&mut self, egl: &Egl, renderer: &mut Renderer, wl: &CompositorState) -> bool {
        let now = Instant::now();
        self.overlay.tick(wl, now);
        self.model.tick(now);
        self.settle(wl);
        let open = self.overlay.is_open() && self.model.is_open();
        self.stale = false;
        let opacity = self.overlay.opacity();
        let (width, height) = (self.overlay.width() as f32, self.overlay.height() as f32);
        let Some(window) = self.overlay.window_mut() else { return false };
        if !window.begin(egl, renderer) {
            return false;
        }
        let scale = window.scale;
        if std::mem::take(&mut self.release_thumbnails) {
            self.canvas.release_thumbnails(renderer);
        }
        let mut frame = self.canvas.begin(renderer, scale);
        frame.set_opacity(opacity);
        self.frame = Frame::default();
        if open {
            self.model.sync();
            self.art.uptime = user::uptime();
            self.frame = view::paint(&mut frame, &mut self.model, &self.art, width, height);
        }
        frame.flush();
        if let Some(window) = self.overlay.window_mut() {
            window.end(egl);
        }
        open && (self.overlay.state.animations.has_active() || self.model.animating())
    }

    pub fn destroy(self, egl: &Egl, renderer: &Renderer) {
        if egl.make_current(None) {
            self.canvas.destroy(renderer);
        }
        self.overlay.destroy(egl);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::settings::{BarStyle, parse_config};
    use crate::service::runtime;
    use crate::service::settings::SettingsService;
    use calloop::EventLoop;

    #[test]
    fn settings_hooks_write_through() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("config.json");
        std::fs::write(&path, "{}").unwrap();
        let event_loop: EventLoop<'static, ()> = EventLoop::try_new().unwrap();
        runtime::attach(&event_loop.handle()).unwrap();
        let service = SettingsService::start_with(path.to_str().unwrap().to_string());
        let outputs = Rc::new(RefCell::new(vec!["DP-2".to_string(), "DP-1".to_string()]));
        let statuses = DecodeStatuses::default();
        let mut model = Model::new(hooks(service.clone(), outputs.clone(), Box::new(|| "DP-2".to_string()), statuses.clone()));
        let store: Rc<dyn ConfigStore> = service.clone();
        store.update(&mut |c| c.bar_style = BarStyle::Okinami);
        assert_eq!(model.config().bar_style, BarStyle::Okinami, "the model sees the edit at once");
        let on_disk = parse_config(&std::fs::read_to_string(&path).unwrap());
        assert_eq!(on_disk.bar_style, BarStyle::Okinami, "the edit is written to the file");
        outputs.borrow_mut().push("HDMI-A-1".into());
        model.sync();
        assert_eq!(model.monitors(), ["DP-1", "DP-2", "HDMI-A-1"], "the monitors follow the outputs, sorted");

        statuses.borrow_mut().insert("DP-2".into(), vec![DecodeStatus::ZeroCopy, DecodeStatus::CpuFallback]);
        assert!(model.cpu_fallback("DP-2", 1), "a column decoding on the CPU warns");
        assert!(!model.cpu_fallback("DP-2", 0) && !model.cpu_fallback("DP-1", 0) && !model.cpu_fallback("DP-2", -1), "zero-copy, unknown and out-of-range columns do not");
    }
}
