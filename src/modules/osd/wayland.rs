//! The OSD surface of one output: a small card above the bottom edge that never takes input.

use std::time::Instant;

use smithay_client_toolkit::compositor::{CompositorState, Region};
use smithay_client_toolkit::shell::WaylandSurface;
use smithay_client_toolkit::shell::wlr_layer::{Anchor, KeyboardInteractivity, Layer, LayerShell};
use wayland_client::protocol::wl_output::WlOutput;
use wayland_client::protocol::wl_surface::WlSurface;
use wayland_client::{Proxy, QueueHandle};

use crate::wayland::State;
use crate::wayland::render::egl::Egl;
use crate::wayland::render::gl_canvas::GlCanvas;
use crate::wayland::render::layer_window::LayerWindow;
use crate::wayland::render::renderer::Renderer;
use crate::config::osd_config as cfg;
use crate::modules::osd::model::{Kind, Model};
use crate::modules::osd::view;

pub const LAYER_NAMESPACE: &str = "astralia-shell-osd";

/// A show request, coalesced with others between two loop wakeups.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct OsdShow {
    pub kind: Kind,
    pub percent: i32,
    pub muted: bool,
}

pub struct Osd {
    pub window: LayerWindow,
    canvas: GlCanvas,
    model: Model,
}

impl Osd {
    pub fn new(compositor: &CompositorState, layer_shell: &LayerShell, qh: &QueueHandle<State>, output: &WlOutput) -> Osd {
        let surface = compositor.create_surface(qh);
        let layer = layer_shell.create_layer_surface(qh, surface, Layer::Overlay, Some(LAYER_NAMESPACE), Some(output));
        layer.set_anchor(Anchor::BOTTOM);
        layer.set_size(cfg::WIDTH as u32, cfg::HEIGHT as u32);
        layer.set_margin(0, 0, cfg::MARGIN_BOTTOM, 0);
        layer.set_exclusive_zone(-1);
        layer.set_keyboard_interactivity(KeyboardInteractivity::None);
        match Region::new(compositor) {
            Ok(empty) => layer.wl_surface().set_input_region(Some(empty.wl_region())),
            Err(e) => ::log::error!("osd: no input region: {e}"),
        }
        layer.commit();
        Osd { window: LayerWindow::new(layer, cfg::HEIGHT), canvas: GlCanvas::new(), model: Model::new(Instant::now()) }
    }

    pub fn owns(&self, surface: &WlSurface) -> bool {
        self.window.owns(surface)
    }

    pub fn surface_id(&self) -> wayland_client::backend::ObjectId {
        self.window.layer.wl_surface().id()
    }

    pub fn configure(&mut self, egl: &Egl, width: i32) {
        self.window.configure(egl, if width > 0 { width } else { cfg::WIDTH });
    }

    /// True when the card changed and wants a frame.
    pub fn show(&mut self, request: OsdShow) -> bool {
        self.model.show(request.kind, request.percent, request.muted, Instant::now())
    }

    /// The wait until the card starts hiding.
    pub fn until_hide(&self, now: Instant) -> std::time::Duration {
        self.model.until_hide(now)
    }

    /// Starts hiding a card past its deadline; true when it did.
    pub fn tick_hide(&mut self, now: Instant) -> bool {
        let due = self.model.visible() && now >= self.model.hide_at();
        if due {
            self.model.hide();
        }
        due
    }

    /// Paints one frame; true while the card animates, so the caller asks for another.
    pub fn draw(&mut self, egl: &Egl, renderer: &mut Renderer) -> bool {
        if !self.window.begin(egl, renderer) {
            return false;
        }
        self.model.tick(self.window.frame_time());
        let mut frame = self.canvas.begin(renderer, self.window.scale);
        view::paint(&mut frame, &self.model);
        frame.flush();
        self.window.end(egl);
        self.model.animating()
    }

    pub fn destroy(self, egl: &Egl, renderer: &Renderer) {
        if egl.make_current(None) {
            self.canvas.destroy(renderer);
        }
        self.window.destroy(egl);
    }
}
