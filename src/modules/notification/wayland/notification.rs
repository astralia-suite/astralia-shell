//! The notification surface of one output: the card stack in the bottom-right corner. It takes input
//! only over close buttons and clickable cards.

use std::time::Instant;

use smithay_client_toolkit::compositor::{CompositorState, Region};
use smithay_client_toolkit::shell::WaylandSurface;
use smithay_client_toolkit::shell::wlr_layer::{Anchor, KeyboardInteractivity, Layer, LayerShell};
use wayland_client::protocol::wl_output::WlOutput;
use wayland_client::protocol::wl_surface::WlSurface;
use wayland_client::{Proxy, QueueHandle};

use crate::backend::wayland::State;
use crate::backend::wayland::render::egl::Egl;
use crate::backend::wayland::render::gl_canvas::GlCanvas;
use crate::backend::wayland::render::layer_window::LayerWindow;
use crate::backend::wayland::render::renderer::Renderer;
use crate::config::notification_config as cfg;
use crate::modules::notification::model::{Boxes, Model, Press, ViewState, card_boxes, close_boxes, input_boxes, layout_notifications};
use crate::modules::notification::view;
use crate::render::canvas::{Canvas, GroupOptions};
use crate::render::geometry::Rect;

pub const LAYER_NAMESPACE: &str = "astralia-shell-notification";

const SURFACE_W: i32 = cfg::CARD_WIDTH as i32;
const SURFACE_H: i32 = cfg::MAX_STACK_HEIGHT as i32;

pub struct Notifications {
    pub window: LayerWindow,
    canvas: GlCanvas,
    view: ViewState,
    close: Boxes,
    cards: Boxes,
    /// The input region last sent; `None` before the first frame, when it is empty.
    input: Option<Boxes>,
}

fn set_region(surface: &WlSurface, compositor: &CompositorState, boxes: &Boxes) {
    match Region::new(compositor) {
        Ok(region) => {
            for (_, r) in boxes {
                region.add(r.x as i32, r.y as i32, r.w as i32, r.h as i32);
            }
            surface.set_input_region(Some(region.wl_region()));
        }
        Err(e) => ::log::error!("notification: no input region: {e}"),
    }
}

/// Moves stack-relative boxes onto the surface.
fn on_surface(mut boxes: Boxes, origin: f32) -> Boxes {
    for (_, r) in &mut boxes {
        r.y += origin;
    }
    boxes
}

impl Notifications {
    pub fn new(compositor: &CompositorState, layer_shell: &LayerShell, qh: &QueueHandle<State>, output: &WlOutput) -> Notifications {
        let surface = compositor.create_surface(qh);
        let layer = layer_shell.create_layer_surface(qh, surface, Layer::Top, Some(LAYER_NAMESPACE), Some(output));
        layer.set_anchor(Anchor::BOTTOM | Anchor::RIGHT);
        layer.set_size(SURFACE_W as u32, SURFACE_H as u32);
        layer.set_margin(0, cfg::MARGIN_RIGHT, cfg::MARGIN_BOTTOM, 0);
        layer.set_exclusive_zone(-1);
        layer.set_keyboard_interactivity(KeyboardInteractivity::None);
        set_region(layer.wl_surface(), compositor, &Vec::new());
        layer.commit();
        Notifications { window: LayerWindow::new(layer, SURFACE_H), canvas: GlCanvas::new(), view: ViewState::default(), close: Vec::new(), cards: Vec::new(), input: None }
    }

    pub fn owns(&self, surface: &WlSurface) -> bool {
        self.window.owns(surface)
    }

    pub fn surface_id(&self) -> wayland_client::backend::ObjectId {
        self.window.layer.wl_surface().id()
    }

    pub fn configure(&mut self, egl: &Egl, width: i32) {
        self.window.configure(egl, if width > 0 { width } else { SURFACE_W });
    }

    /// Paints the stack and updates the input region; true while anything animates.
    pub fn draw(&mut self, egl: &Egl, renderer: &mut Renderer, compositor: &CompositorState, model: &mut Model) -> bool {
        let now = Instant::now();
        model.tick(now);
        self.view.tick(now);
        self.view.forget_missing(model.entries());
        if !self.window.begin(egl, renderer) {
            return false;
        }
        let mut frame = self.canvas.begin(renderer, self.window.scale);
        let layout = layout_notifications(&mut frame, model, &self.view, SURFACE_H as f32);
        let origin = SURFACE_H as f32 - layout.height;
        frame.begin_group(Rect { x: 0.0, y: origin, w: SURFACE_W as f32, h: layout.height }, GroupOptions::default());
        view::paint(&mut frame, model, &self.view, &layout);
        frame.end_group();
        frame.flush();
        self.close = on_surface(close_boxes(&layout, &self.view), origin);
        self.cards = on_surface(card_boxes(&layout, &self.view), origin);
        let input = on_surface(input_boxes(&layout, &self.view), origin);
        if self.input.as_ref() != Some(&input) {
            set_region(self.window.layer.wl_surface(), compositor, &input);
            self.input = Some(input);
        }
        self.window.end(egl);
        model.animating() || self.view.animating()
    }

    /// The pointer over the stack (`Some`) or gone (`None`); true when a hover changed.
    pub fn pointer(&mut self, at: Option<(f64, f64)>) -> bool {
        match at {
            Some((x, y)) => self.view.set_pointer(&self.close, &self.cards, x, y),
            None => self.view.clear_pointer(),
        }
    }

    pub fn press(&mut self, x: f64, y: f64) -> Press {
        self.view.press(&self.close, &self.cards, x, y)
    }

    pub fn wants_hand(&self) -> bool {
        self.view.wants_hand()
    }

    pub fn destroy(self, egl: &Egl, renderer: &Renderer) {
        if egl.make_current(None) {
            self.canvas.destroy(renderer);
        }
        self.window.destroy(egl);
    }
}
