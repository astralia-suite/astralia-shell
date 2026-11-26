//! An `xdg_popup` child of a layer surface, placed below an anchor rectangle and grabbing the pointer.

use smithay_client_toolkit::compositor::CompositorState;
use smithay_client_toolkit::shell::WaylandSurface;
use smithay_client_toolkit::shell::wlr_layer::LayerSurface;
use smithay_client_toolkit::shell::xdg::popup::Popup;
use smithay_client_toolkit::shell::xdg::{XdgPositioner, XdgShell};
use wayland_client::QueueHandle;
use wayland_client::protocol::wl_seat::WlSeat;
use wayland_client::protocol::wl_surface::WlSurface;
use smithay_client_toolkit::reexports::protocols::xdg::shell::client::xdg_positioner::{Anchor, ConstraintAdjustment, Gravity};

use super::egl::Egl;
use super::layer_window::LayerWindow;
use crate::wayland::State;
use crate::render::geometry::Rect;

/// The popup as a `WaylandSurface`; dropping it destroys the `xdg_popup`, `xdg_surface` and `wl_surface`.
pub struct PopupRole(pub Popup);

impl WaylandSurface for PopupRole {
    fn wl_surface(&self) -> &WlSurface {
        self.0.wl_surface()
    }
}

fn positioner(xdg: &XdgShell, anchor: Rect, w: i32, h: i32) -> Option<XdgPositioner> {
    let p = XdgPositioner::new(xdg).map_err(|e| ::log::error!("popup: no positioner: {e}")).ok()?;
    p.set_size(w.max(1), h.max(1));
    p.set_anchor_rect(anchor.x as i32, anchor.y as i32, anchor.w.max(1.0) as i32, anchor.h.max(1.0) as i32);
    p.set_anchor(Anchor::BottomLeft);
    p.set_gravity(Gravity::BottomRight);
    p.set_constraint_adjustment(ConstraintAdjustment::SlideX | ConstraintAdjustment::SlideY | ConstraintAdjustment::FlipY);
    Some(p)
}

pub struct PopupWindow {
    pub window: LayerWindow<PopupRole>,
    pub configured: bool,
    token: u32,
}

impl PopupWindow {
    /// Creates the popup under `parent` and grabs with the press `serial`; the EGL surface comes with the first configure.
    #[allow(clippy::too_many_arguments)]
    pub fn new(compositor: &CompositorState, xdg: &XdgShell, qh: &QueueHandle<State>, parent: &LayerSurface, anchor: Rect, w: i32, h: i32, seat: Option<&WlSeat>, serial: u32) -> Option<PopupWindow> {
        let positioner = positioner(xdg, anchor, w, h)?;
        let surface = compositor.create_surface(qh);
        let popup = Popup::from_surface(None, &positioner, qh, surface, xdg).map_err(|e| ::log::error!("popup: {e}")).ok()?;
        parent.get_popup(popup.xdg_popup());
        if let Some(seat) = seat {
            popup.xdg_popup().grab(seat, serial);
        }
        popup.xdg_surface().set_window_geometry(0, 0, w, h);
        popup.wl_surface().commit();
        let mut window = LayerWindow::new(PopupRole(popup), h);
        window.width = w;
        Some(PopupWindow { window, configured: false, token: 0 })
    }

    /// The compositor's size wins when it gives one.
    pub fn configure(&mut self, egl: &Egl, width: i32, height: i32) {
        let w = if width > 0 { width } else { self.window.width };
        if height > 0 {
            self.window.set_height(height);
        }
        self.window.configure(egl, w);
        self.configured = true;
    }

    pub fn reposition(&mut self, xdg: &XdgShell, anchor: Rect, w: i32, h: i32) {
        let Some(positioner) = positioner(xdg, anchor, w, h) else { return };
        self.token += 1;
        let popup = &self.window.layer.0;
        popup.reposition(&positioner, self.token);
        popup.xdg_surface().set_window_geometry(0, 0, w, h);
        self.configured = false;
        popup.wl_surface().commit();
        self.window.width = w;
        self.window.set_height(h);
    }

    pub fn is(&self, popup: &Popup) -> bool {
        &self.window.layer.0 == popup
    }

    pub fn owns(&self, surface: &WlSurface) -> bool {
        self.window.owns(surface)
    }

    pub fn destroy(self, egl: &Egl) {
        self.window.destroy(egl);
    }
}
