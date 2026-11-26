//! A full-output overlay layer surface for cards: it takes input and the keyboard only while open,
//! and stays mapped across opens and closes.

use smithay_client_toolkit::compositor::CompositorState;
use smithay_client_toolkit::shell::WaylandSurface;
use smithay_client_toolkit::shell::wlr_layer::{Anchor, KeyboardInteractivity, Layer, LayerShell};
use wayland_client::QueueHandle;
use wayland_client::protocol::wl_output::WlOutput;
use wayland_client::protocol::wl_surface::WlSurface;

use super::egl::Egl;
use super::layer_window::LayerWindow;
use crate::wayland::State;

pub struct OverlayPanel {
    pub window: LayerWindow,
    open: bool,
}

impl OverlayPanel {
    pub fn new(compositor: &CompositorState, layer_shell: &LayerShell, qh: &QueueHandle<State>, output: &WlOutput, namespace: &str) -> OverlayPanel {
        let surface = compositor.create_surface(qh);
        let layer = layer_shell.create_layer_surface(qh, surface, Layer::Overlay, Some(namespace.to_string()), Some(output));
        layer.set_anchor(Anchor::TOP | Anchor::BOTTOM | Anchor::LEFT | Anchor::RIGHT);
        layer.set_exclusive_zone(-1);
        layer.set_keyboard_interactivity(KeyboardInteractivity::None);
        let panel = OverlayPanel { window: LayerWindow::new(layer, 1), open: false };
        panel.update_input_region(compositor);
        panel.window.layer.commit();
        panel
    }

    pub fn configure(&mut self, egl: &Egl, width: i32, height: i32) {
        self.window.set_height(height);
        self.window.configure(egl, width);
    }

    pub fn is_open(&self) -> bool {
        self.open
    }

    fn update_input_region(&self, compositor: &CompositorState) {
        super::overlay::set_input_region(self.window.layer.wl_surface(), compositor, self.open);
    }

    /// Takes or gives back the keyboard and the input region; nothing when already in that state.
    pub fn set_open(&mut self, compositor: &CompositorState, open: bool) {
        if self.open == open {
            return;
        }
        self.open = open;
        let interactivity = if open { KeyboardInteractivity::Exclusive } else { KeyboardInteractivity::None };
        self.window.layer.set_keyboard_interactivity(interactivity);
        self.update_input_region(compositor);
        self.window.layer.commit();
    }

    /// Asks for a frame while open; true when the caller draws now.
    pub fn request_frame(&mut self, qh: &QueueHandle<State>) -> bool {
        self.open && self.window.request_frame(qh)
    }

    pub fn owns(&self, surface: &WlSurface) -> bool {
        self.window.owns(surface)
    }

    pub fn destroy(self, egl: &Egl) {
        self.window.destroy(egl);
    }
}
