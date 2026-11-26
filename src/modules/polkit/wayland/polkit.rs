//! The polkit overlay surface: the authentication card, opened while polkitd waits for an answer.

use std::cell::Cell;
use std::rc::Rc;
use std::time::Instant;

use smithay_client_toolkit::compositor::CompositorState;
use wayland_client::QueueHandle;

use crate::wayland::State;
use crate::wayland::render::egl::Egl;
use crate::wayland::render::gl_canvas::GlCanvas;
use crate::wayland::render::overlay::Overlay;
use crate::wayland::render::renderer::Renderer;
use crate::base::input::KeyEvent;
use crate::config::polkit_config as cfg;
use crate::modules::polkit::model::{Key, Model, prompt};
use crate::modules::polkit::view;
use crate::service::polkit::PolkitService;

pub struct Polkit {
    pub overlay: Overlay,
    canvas: GlCanvas,
    model: Model,
    /// Set by the model when its close ends; the host then gives the keyboard back.
    closed: Rc<Cell<bool>>,
    /// A closed card still shows its last frame until it draws once more.
    stale: bool,
}

impl Polkit {
    /// `wake` runs when the echo glyph has decoded, so the card repaints.
    pub fn new(wake: Rc<dyn Fn()>) -> Polkit {
        let mut model = Model::default();
        let closed = Rc::new(Cell::new(false));
        let flag = closed.clone();
        model.on_closed = Some(Box::new(move || flag.set(true)));
        let mut canvas = GlCanvas::new();
        canvas.set_on_image_ready(wake);
        Polkit { overlay: Overlay::new(cfg::LAYER_NAMESPACE), canvas, model, closed, stale: false }
    }

    pub fn is_open(&self) -> bool {
        self.overlay.is_open()
    }

    /// Opens or closes to follow whether a request is pending. A closed card must be bound first.
    pub fn sync(&mut self, compositor: &CompositorState, pending: bool) {
        if pending && !self.overlay.is_open_or_pending() {
            // Before the first configure the open waits in the overlay; `configure` starts the model then.
            if self.overlay.toggle(compositor, false) && self.overlay.is_open() {
                self.open_model();
            }
            return;
        }
        if pending {
            if self.model.closing() {
                self.model.sync(true);
            }
            return;
        }
        if self.overlay.is_open() {
            self.model.sync(false);
            self.settle(compositor);
        } else if self.overlay.is_open_or_pending() {
            // Answered before the surface was configured: drop the queued open.
            self.overlay.toggle(compositor, false);
        }
    }

    fn open_model(&mut self) {
        self.model.reset();
        self.model.sync(true);
    }

    /// Gives the keyboard back once the close has finished.
    fn settle(&mut self, compositor: &CompositorState) {
        if self.closed.replace(false) {
            self.stale = true;
            self.overlay.release(compositor);
        }
    }

    pub fn configure(&mut self, egl: &Egl, compositor: &CompositorState, width: i32, height: i32) {
        if self.overlay.configure(egl, compositor, width, height) {
            self.open_model();
        }
    }

    pub fn key(&mut self, service: &PolkitService, event: &KeyEvent) {
        if !self.overlay.is_open() {
            return;
        }
        match self.model.key(event) {
            Key::Submit => {
                // `respond` wipes the password whether or not it was sent.
                let mut password = self.model.take_password();
                service.respond(&mut password);
            }
            Key::Cancel => service.cancel(),
            Key::Changed | Key::None => {}
        }
    }

    /// Asks for a frame while open, and once more after closing to clear the surface; true when the caller draws now.
    pub fn request_frame(&mut self, qh: &QueueHandle<State>) -> bool {
        let wanted = self.overlay.is_open() || self.stale;
        wanted && self.overlay.window_mut().is_some_and(|w| w.request_frame(qh))
    }

    /// Paints one frame; true while the card animates, so the caller asks for another.
    pub fn draw(&mut self, egl: &Egl, renderer: &mut Renderer, compositor: &CompositorState, service: Option<&PolkitService>) -> bool {
        self.model.tick(Instant::now());
        self.settle(compositor);
        self.stale = false;
        let (width, height) = (self.overlay.width() as f32, self.overlay.height() as f32);
        let Some(window) = self.overlay.window_mut() else { return false };
        if !window.begin(egl, renderer) {
            return false;
        }
        let scale = window.scale;
        let mut frame = self.canvas.begin(renderer, scale);
        if self.model.open() {
            if let Some(service) = service {
                self.model.set_prompt(prompt(service));
            }
            view::paint(&mut frame, &mut self.model, width, height);
        }
        frame.flush();
        if let Some(window) = self.overlay.window_mut() {
            window.end(egl);
        }
        self.model.animating()
    }

    pub fn destroy(self, egl: &Egl, renderer: &Renderer) {
        if egl.make_current(None) {
            self.canvas.destroy(renderer);
        }
        self.overlay.destroy(egl);
    }
}
