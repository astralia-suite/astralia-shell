//! The overview overlay surface: the grid with live window thumbnails.

use std::rc::Rc;
use std::time::Duration;

use smithay_client_toolkit::compositor::CompositorState;
use wayland_client::QueueHandle;

use crate::wayland::State;
use crate::wayland::capture::ToplevelExportState;
use crate::wayland::render::egl::Egl;
use crate::wayland::render::gl_canvas::{GlCanvas, GlFrame};
use crate::wayland::render::overlay::Overlay;
use crate::wayland::render::renderer::Renderer;
use crate::wayland::toplevel::{ToplevelList, toplevel_handles_for};
use crate::base::input::KeyEvent;
use crate::config::overview_config as cfg;
use crate::modules::overview::model::{Model, Notify, Tile};
use crate::modules::overview::view;
use crate::render::canvas::Canvas;
use crate::render::geometry::Rect;
use crate::service::compositor::Compositor;

pub const LAYER_NAMESPACE: &str = "astralia-shell-overview";

/// How often thumbnails refresh while the overview is open.
pub const CAPTURE_INTERVAL: Duration = Duration::from_millis(cfg::CAPTURE_INTERVAL_MS as u64);

pub struct Overview {
    pub overlay: Overlay,
    canvas: GlCanvas,
    model: Model,
    compositor: Rc<dyn Compositor>,
    /// The output the overview was opened on.
    output: String,
    pub opened_by_widget: bool,
    /// A closed overlay still shows its last frame until it draws once more.
    stale: bool,
    hover_clickable: bool,
}

impl Overview {
    pub fn new(compositor: Rc<dyn Compositor>, notify: Notify) -> Overview {
        let mut model = Model::new(compositor.clone());
        model.on_notify = Some(notify);
        Overview { overlay: Overlay::new(LAYER_NAMESPACE), canvas: GlCanvas::new(), model, compositor, output: String::new(), opened_by_widget: false, stale: false, hover_clickable: false }
    }

    pub fn is_open(&self) -> bool {
        self.overlay.is_open()
    }

    /// Open on `output` from the bar's workspace info item.
    pub fn shown_on(&self, output: &str) -> bool {
        self.is_open() && self.opened_by_widget && self.output == output
    }

    fn available(&self) -> bool {
        self.compositor.name() != "none"
    }

    /// Opens on `output` with the scroll rising, or plays the close.
    pub fn toggle(&mut self, wl: &CompositorState, output: &str, by_widget: bool) {
        if !self.available() {
            return;
        }
        if !self.overlay.is_open() {
            if self.overlay.toggle(wl, false) {
                self.compositor.refresh();
                self.opened_by_widget = by_widget;
                self.output = output.to_string();
                // Before the first configure the open waits in the overlay; `configure` starts the model then.
                if self.overlay.is_open() {
                    self.model.open(&self.output, self.overlay.width() as f32, self.overlay.height() as f32);
                }
            }
            return;
        }
        self.model.close();
        self.settle(wl);
    }

    /// Closes at once, without the exit.
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
            self.opened_by_widget = false;
            self.hover_clickable = false;
            self.stale = true;
            self.overlay.release(wl);
        }
    }

    pub fn configure(&mut self, egl: &Egl, wl: &CompositorState, width: i32, height: i32) {
        if self.overlay.configure(egl, wl, width, height) {
            self.model.open(&self.output, width as f32, height as f32);
        }
    }

    pub fn key(&mut self, wl: &CompositorState, event: &KeyEvent) {
        if self.overlay.is_open() {
            self.model.key(event);
            self.settle(wl);
        }
    }

    pub fn press(&mut self, wl: &CompositorState, x: f64, y: f64, shift: bool, alt: bool) {
        self.model.press(x, y, shift, alt);
        self.settle(wl);
    }

    /// The pointer over the overlay (`Some`) or gone (`None`).
    pub fn pointer(&mut self, at: Option<(f64, f64)>) {
        match at {
            Some((x, y)) => {
                self.model.move_to(x, y);
                self.hover_clickable = self.overlay.is_open() && self.model.clickable(x, y);
            }
            None => self.hover_clickable = false,
        }
    }

    pub fn release(&mut self) {
        self.model.release();
    }

    pub fn wants_hand(&self) -> bool {
        self.hover_clickable
    }

    /// Asks for a frame while open, and once more after closing to clear the surface; true when the caller draws now.
    pub fn request_frame(&mut self, qh: &QueueHandle<State>) -> bool {
        self.overlay.request_frame(self.stale, qh)
    }

    /// Paints one frame, asking for fresh thumbnails of the drawn windows; true while a tween or a
    /// drag runs, so the caller asks for another.
    #[allow(clippy::too_many_arguments)]
    pub fn draw(&mut self, egl: &Egl, renderer: &mut Renderer, wl: &CompositorState, capture: &mut ToplevelExportState, toplevels: &ToplevelList, qh: &QueueHandle<State>) -> bool {
        self.model.tick(self.overlay.frame_time());
        self.settle(wl);
        let open = self.overlay.is_open() && self.available();
        self.stale = false;
        let (width, height) = (self.overlay.width() as f32, self.overlay.height() as f32);
        if open {
            self.model.sync(width, height);
            if self.model.grid_alpha() > 0.0 {
                self.request_captures(capture, toplevels, qh);
            }
        }
        let opacity = self.overlay.opacity();
        let Some(window) = self.overlay.window_mut() else { return false };
        if !window.begin(egl, renderer) {
            return false;
        }
        let scale = window.scale;
        capture.upload(renderer);
        let mut frame = self.canvas.begin(renderer, scale);
        frame.set_opacity(opacity);
        if open {
            view::paint(&mut frame, &mut self.model, &mut |frame: &mut GlFrame, tile: &Tile, rect: Rect, radius: f32| match capture.texture(&tile.address) {
                Some(tex) => {
                    frame.texture(tex, rect, radius);
                    true
                }
                None => false,
            });
        }
        frame.flush();
        if let Some(window) = self.overlay.window_mut() {
            window.end(egl);
        }
        let live: Vec<String> = if open { self.compositor.state().clients.iter().map(|c| c.address.clone()).collect() } else { Vec::new() };
        capture.prune(renderer, &live);
        self.model.animating() || self.model.dragging()
    }

    /// One capture request per tile, through its toplevel handle where the compositor needs one.
    fn request_captures(&self, capture: &mut ToplevelExportState, toplevels: &ToplevelList, qh: &QueueHandle<State>) {
        let by_handle = self.compositor.capture_by_toplevel_handle();
        let handles = if by_handle { toplevel_handles_for(toplevels, &self.compositor.state().clients) } else { Default::default() };
        for tile in self.model.tiles() {
            let toplevel = handles.get(&tile.address);
            if !by_handle || toplevel.is_some() {
                capture.request(qh, &tile.address, toplevel, CAPTURE_INTERVAL);
            }
        }
    }

    pub fn destroy(self, egl: &Egl, renderer: &Renderer) {
        if egl.make_current(None) {
            self.canvas.destroy(renderer);
        }
        self.overlay.destroy(egl);
    }
}
