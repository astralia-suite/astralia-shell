//! The wallpaper picker overlay surface: the carousel over one wallpaper column.

use std::rc::Rc;

use smithay_client_toolkit::compositor::CompositorState;
use wayland_client::QueueHandle;

use crate::wayland::State;
use crate::wayland::render::egl::Egl;
use crate::wayland::render::gl_canvas::GlCanvas;
use crate::wayland::render::overlay::Overlay;
use crate::wayland::render::renderer::Renderer;
use crate::base::input::KeyEvent;
use crate::config::wallpaper_select_config as cfg;
use crate::modules::wallpaper_select::model::{Hooks, Model};
use crate::modules::wallpaper_select::view;

pub const LAYER_NAMESPACE: &str = "astralia-shell-wallpaper-select";

pub struct WallpaperSelect {
    pub overlay: Overlay,
    canvas: GlCanvas,
    model: Model,
    /// A closed overlay still shows its last frame until it draws once more.
    stale: bool,
    /// Closed: the next frame drops the thumbnails.
    release_thumbnails: bool,
    hover_clickable: bool,
}

impl WallpaperSelect {
    /// `wake` runs when a scan lands or a thumbnail decodes, so the host polls and repaints.
    pub fn new(hooks: Hooks, wake: Rc<dyn Fn()>) -> WallpaperSelect {
        let mut model = Model::new(hooks);
        model.set_wake(wake.clone());
        let mut canvas = GlCanvas::new();
        canvas.set_on_image_ready(wake);
        WallpaperSelect { overlay: Overlay::new(LAYER_NAMESPACE), canvas, model, stale: false, release_thumbnails: false, hover_clickable: false }
    }

    pub fn is_open(&self) -> bool {
        self.overlay.is_open()
    }

    /// The column of `monitor` under a point of a `width` by `height` monitor.
    pub fn column_at(&self, monitor: &str, x: f64, y: f64, width: f64, height: f64) -> i32 {
        self.model.column_at(monitor, x, y, width, height)
    }

    /// Opens on `column` of `monitor`; the overlay must be bound to that monitor first.
    pub fn open(&mut self, wl: &CompositorState, monitor: &str, column: i32) {
        if self.overlay.is_open() {
            return;
        }
        // A picker whose output went away is still open in the model.
        self.model.fast_close();
        self.model.take_closed();
        self.model.open(monitor, column);
        if !self.overlay.toggle(wl, false) {
            self.model.fast_close();
            self.model.take_closed();
        }
    }

    /// Fades the cards out; the overlay gives the input back once they are gone.
    pub fn close(&mut self, wl: &CompositorState) {
        if self.overlay.is_open() {
            self.model.close();
            self.settle(wl);
        }
    }

    /// Closes at once, without the fade.
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

    /// Applies a finished scan.
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
        self.model.scroll(dy as f32 / cfg::WAYLAND_WHEEL_UNIT);
    }

    /// The pointer over the overlay (`Some`) or gone (`None`).
    pub fn pointer(&mut self, at: Option<(f64, f64)>) {
        self.hover_clickable = at.is_some_and(|(x, y)| self.overlay.is_open() && self.model.clickable(x, y));
    }

    pub fn wants_hand(&self) -> bool {
        self.hover_clickable
    }

    /// Asks for a frame while open, and once more after closing to clear the surface; true when the caller draws now.
    pub fn request_frame(&mut self, qh: &QueueHandle<State>) -> bool {
        self.overlay.request_frame(self.stale, qh)
    }

    /// Paints one frame; true while a tween or an image fade runs, so the caller asks for another.
    pub fn draw(&mut self, egl: &Egl, renderer: &mut Renderer, wl: &CompositorState) -> bool {
        self.model.tick(self.overlay.frame_time());
        self.settle(wl);
        let open = self.overlay.is_open() && self.model.is_open();
        self.stale = false;
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
        if open {
            view::paint(&mut frame, &mut self.model, width, height);
        }
        frame.flush();
        if let Some(window) = self.overlay.window_mut() {
            window.end(egl);
        }
        open && self.model.animating()
    }

    pub fn destroy(self, egl: &Egl, renderer: &Renderer) {
        if egl.make_current(None) {
            self.canvas.destroy(renderer);
        }
        self.overlay.destroy(egl);
    }
}
