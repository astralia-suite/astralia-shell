//! One output's bar panels: an overlay layer surface for the open card and an `xdg_popup` for its menu.

use std::rc::Rc;
use std::time::Instant;

use smithay_client_toolkit::compositor::CompositorState as WlCompositorState;
use smithay_client_toolkit::shell::WaylandSurface;
use smithay_client_toolkit::shell::wlr_layer::LayerShell;
use smithay_client_toolkit::shell::xdg::popup::Popup;
use wayland_client::backend::ObjectId;
use wayland_client::protocol::wl_output::WlOutput;
use wayland_client::protocol::wl_surface::WlSurface;
use wayland_client::{Proxy, QueueHandle};

use crate::backend::wayland::monitor::Redraw;
use crate::backend::wayland::render::egl::Egl;
use crate::backend::wayland::render::gl_canvas::GlCanvas;
use crate::backend::wayland::render::overlay_panel::OverlayPanel;
use crate::backend::wayland::render::popup_window::PopupWindow;
use crate::backend::wayland::render::renderer::Renderer;
use crate::backend::wayland::{Ctx, State};
use crate::base::input::{Button, KeyEvent};
use crate::modules::bar::model::Item;
use crate::modules::bar::model::items::item_for;
use crate::modules::bar::panel::content::{PanelSources, add_panels};
use crate::modules::bar::panel::core::panel_set::{PanelAnchor, PanelId, PanelSet};
use crate::render::geometry::Rect;

pub struct PanelSurface {
    set: PanelSet,
    overlay: OverlayPanel,
    canvas: GlCanvas,
    popup: Option<PopupWindow>,
    popup_canvas: GlCanvas,
    /// Anchor and size the popup was last placed with.
    popup_box: (Rect, f32, f32),
    press_serial: u32,
    /// Where the cards' upper roller sits: below the bar and its gap.
    pub card_top: f32,
}

impl PanelSurface {
    pub fn new(compositor: &WlCompositorState, layer_shell: &LayerShell, qh: &QueueHandle<State>, output: &WlOutput, redraw: &Redraw, card_top: f32, sources: &PanelSources) -> PanelSurface {
        let mut set = PanelSet::new();
        add_panels(&mut set, sources);
        let changed = redraw.clone();
        set.on_changed.set(Rc::new(move || changed.panels()));
        let state = redraw.clone();
        set.on_state.set(Rc::new(move |_, _| state.all()));
        let mut canvas = GlCanvas::new();
        let mut popup_canvas = GlCanvas::new();
        let images = redraw.clone();
        let notify: Rc<dyn Fn()> = Rc::new(move || images.panels());
        canvas.set_on_image_ready(notify.clone());
        popup_canvas.set_on_image_ready(notify);
        PanelSurface {
            set,
            overlay: OverlayPanel::new(compositor, layer_shell, qh, output, "astralia-shell-panel"),
            canvas,
            popup: None,
            popup_canvas,
            popup_box: (Rect::default(), 0.0, 0.0),
            press_serial: 0,
            card_top,
        }
    }

    pub fn owns(&self, surface: &WlSurface) -> bool {
        self.overlay.owns(surface) || self.popup.as_ref().is_some_and(|p| p.owns(surface))
    }

    pub fn owns_overlay(&self, surface: &WlSurface) -> bool {
        self.overlay.owns(surface)
    }

    /// Every surface this panel host has, for dropping input focus when they go.
    pub fn surface_ids(&self) -> Vec<ObjectId> {
        let mut ids = vec![self.overlay.window.layer.wl_surface().id()];
        ids.extend(self.popup.as_ref().map(|p| p.window.layer.wl_surface().id()));
        ids
    }

    pub fn is_open(&self) -> bool {
        self.set.any_open()
    }

    /// The bar item whose panel is open (the active one first), and whether a clock panel is open.
    pub fn open_item(&self) -> (Option<Item>, bool) {
        let open = self.set.active_id().or_else(|| PanelId::ALL.into_iter().find(|&id| self.set.find(id).is_some_and(|p| p.is_open())));
        let clock = [PanelId::Time, PanelId::Date].into_iter().any(|id| self.set.find(id).is_some_and(|p| p.is_open()));
        (open.and_then(item_for), clock)
    }

    pub fn configure(&mut self, egl: &Egl, width: i32, height: i32) {
        self.overlay.configure(egl, width, height);
    }

    pub fn set_scale(&mut self, ctx: &mut Ctx, surface: &WlSurface, scale: i32) {
        if self.overlay.owns(surface) {
            self.overlay.window.set_scale(scale);
            self.request_frame(ctx);
        } else if let Some(popup) = self.popup.as_mut().filter(|p| p.owns(surface)) {
            popup.window.set_scale(scale);
            if popup.window.request_frame(ctx.qh) {
                self.paint_popup(ctx);
            }
        }
    }

    pub fn toggle(&mut self, ctx: &mut Ctx, id: PanelId, anchor: Option<PanelAnchor>) {
        self.set.toggle(id, anchor);
        self.sync_open(ctx);
        self.sync_popup(ctx);
        self.request_frame(ctx);
    }

    /// Plays the close of every open panel, as a press outside them does.
    pub fn close_all(&mut self, ctx: &mut Ctx) {
        if !self.set.any_open() {
            return;
        }
        self.set.close_all();
        self.sync_popup(ctx);
        self.request_frame(ctx);
    }

    fn sync_open(&mut self, ctx: &Ctx) {
        self.overlay.set_open(ctx.compositor, self.set.any_open());
    }

    pub fn request_frame(&mut self, ctx: &mut Ctx) {
        if self.overlay.request_frame(ctx.qh) {
            self.paint(ctx);
        }
    }

    /// A frame callback on one of these surfaces.
    pub fn frame_done(&mut self, ctx: &mut Ctx, surface: &WlSurface) {
        if self.overlay.owns(surface) {
            if self.overlay.window.frame_done(ctx.qh) {
                self.paint(ctx);
            }
        } else if let Some(popup) = self.popup.as_mut().filter(|p| p.owns(surface))
            && popup.window.frame_done(ctx.qh)
        {
            self.paint_popup(ctx);
        }
    }

    fn paint(&mut self, ctx: &mut Ctx) {
        self.set.tick(Instant::now());
        // A close that finished in this tick gives back the keyboard and input before the cleared frame.
        self.sync_open(ctx);
        if self.overlay.window.begin(ctx.egl, ctx.renderer) {
            let (width, scale) = (self.overlay.window.width as f32, self.overlay.window.scale);
            let mut frame = self.canvas.begin(ctx.renderer, scale);
            if self.overlay.is_open() {
                self.set.paint(&mut frame, width, self.card_top);
            }
            frame.flush();
            self.overlay.window.end(ctx.egl);
        }
        self.sync_popup(ctx);
        if self.set.animating() {
            self.request_frame(ctx);
        }
    }

    fn destroy_popup(&mut self, egl: &Egl) {
        if let Some(popup) = self.popup.take() {
            popup.destroy(egl);
        }
        self.popup_box = (Rect::default(), 0.0, 0.0);
    }

    /// Creates, moves or removes the popup to match what the active panel wants.
    fn sync_popup(&mut self, ctx: &mut Ctx) {
        let want = self.set.active().filter(|p| p.popup_wanted()).map(|p| (p.popup_anchor(), p.popup_width(), p.popup_height()));
        let (Some((anchor, w, h)), Some(xdg)) = (want, ctx.xdg) else {
            self.destroy_popup(ctx.egl);
            return;
        };
        let Some(popup) = &mut self.popup else {
            self.popup = PopupWindow::new(ctx.compositor, xdg, ctx.qh, &self.overlay.window.layer, anchor, w as i32, h as i32, ctx.seat, self.press_serial);
            self.popup_box = (anchor, w, h);
            return;
        };
        if self.popup_box != (anchor, w, h) {
            popup.reposition(xdg, anchor, w as i32, h as i32);
            self.popup_box = (anchor, w, h);
        }
        if popup.window.request_frame(ctx.qh) {
            self.paint_popup(ctx);
        }
    }

    fn paint_popup(&mut self, ctx: &mut Ctx) {
        let Some(popup) = self.popup.as_mut().filter(|p| p.configured) else { return };
        let Some(panel) = self.set.active().filter(|p| p.popup_wanted()) else { return };
        if !popup.window.begin(ctx.egl, ctx.renderer) {
            return;
        }
        let mut frame = self.popup_canvas.begin(ctx.renderer, popup.window.scale);
        panel.paint_popup(&mut frame);
        frame.flush();
        popup.window.end(ctx.egl);
    }

    /// The compositor placed `popup`; true when it is this host's.
    pub fn popup_configure(&mut self, ctx: &mut Ctx, popup: &Popup, width: i32, height: i32) -> bool {
        let Some(mine) = self.popup.as_mut().filter(|p| p.is(popup)) else { return false };
        mine.configure(ctx.egl, width, height);
        self.paint_popup(ctx);
        true
    }

    /// The compositor dismissed `popup` (a click elsewhere or a refused grab): it goes, the panel stays.
    pub fn popup_done(&mut self, ctx: &mut Ctx, popup: &Popup) -> bool {
        if !self.popup.as_ref().is_some_and(|p| p.is(popup)) {
            return false;
        }
        self.destroy_popup(ctx.egl);
        if let Some(panel) = self.set.active() {
            panel.dismiss_popup();
        }
        self.request_frame(ctx);
        true
    }

    pub fn press(&mut self, ctx: &mut Ctx, surface: &WlSurface, button: Button, x: f64, y: f64, serial: u32) {
        self.press_serial = serial;
        if self.popup.as_ref().is_some_and(|p| p.owns(surface)) {
            if let Some(panel) = self.set.active() {
                panel.press_popup(x, y, button);
            }
            self.request_frame(ctx);
            return;
        }
        match self.set.active() {
            Some(panel) => {
                panel.press(x, y, button);
            }
            None => self.set.close_all(),
        }
        self.sync_popup(ctx);
        self.request_frame(ctx);
    }

    pub fn wheel(&mut self, surface: &WlSurface, x: f64, y: f64, dy: f64) {
        if self.popup.as_ref().is_some_and(|p| p.owns(surface)) {
            return;
        }
        if let Some(panel) = self.set.active() {
            panel.wheel(x, y, dy);
        }
    }

    pub fn key(&mut self, ctx: &mut Ctx, event: &KeyEvent) {
        if let Some(panel) = self.set.active() {
            panel.key(event);
            self.request_frame(ctx);
        }
    }

    pub fn move_to(&mut self, surface: &WlSurface, x: f64, y: f64) {
        let on_popup = self.popup.as_ref().map(|p| p.owns(surface));
        let Some(panel) = self.set.active() else { return };
        match on_popup {
            Some(true) => {
                panel.hover_popup(x, y);
                return;
            }
            Some(false) => {
                panel.hover_popup(-1.0, -1.0);
            }
            None => {}
        }
        if !panel.move_to(x, y) {
            panel.hover(x, y);
        }
    }

    pub fn release(&mut self) {
        if let Some(panel) = self.set.active() {
            panel.release();
        }
    }

    pub fn wants_hand(&self, surface: &WlSurface, x: f64, y: f64) -> bool {
        if self.popup.as_ref().is_some_and(|p| p.owns(surface)) {
            return self.set.active_id().and_then(|id| self.set.find(id)).is_some_and(|p| p.popup_clickable(x, y));
        }
        self.set.clickable(x, y)
    }

    /// The popup first, then the textures (the context made current without a surface), then the overlay.
    pub fn destroy(mut self, egl: &Egl, renderer: &Renderer) {
        self.destroy_popup(egl);
        if egl.make_current(None) {
            self.canvas.destroy(renderer);
            self.popup_canvas.destroy(renderer);
        }
        self.overlay.destroy(egl);
    }
}
