//! An `xdg_toplevel` window with its `wl_egl_window`, `EGLSurface`, frame clock and fade.

use smithay_client_toolkit::compositor::CompositorState;
use smithay_client_toolkit::shell::WaylandSurface;
use smithay_client_toolkit::shell::xdg::XdgShell;
use smithay_client_toolkit::shell::xdg::window::{Window, WindowConfigure, WindowDecorations};
use wayland_client::QueueHandle;

use super::egl::Egl;
use super::layer_window::LayerWindow;
use super::overlay::{OVERLAY_FADE_MS, OVERLAY_FADE_OWNER};
use crate::backend::wayland::State;
use crate::base::animation::{AnimationManager, Easing, Value, value, writer};
use crate::base::input::{KeyEvent, KeyKind};

/// Size for a configure: each axis of the compositor's size when non-zero, else the window's default.
pub fn toplevel_size(last: (i32, i32), pending: (i32, i32)) -> (i32, i32) {
    (if last.0 > 0 { last.0 } else { pending.0 }, if last.1 > 0 { last.1 } else { pending.1 })
}

/// Where a window is in its life.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Slot {
    Absent,
    /// Created, waiting for its first configure.
    Pending,
    Open,
}

#[derive(Clone, Copy, Debug)]
pub enum Request<'a> {
    /// The window's verb.
    Toggle,
    /// The compositor's `close`.
    Close,
    Key(&'a KeyEvent),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Action {
    Create,
    Destroy,
    Ignore,
}

/// The verb opens a missing window and closes an open one; the compositor's close and `Escape` close it.
pub fn window_request(slot: Slot, request: Request) -> Action {
    match (slot, request) {
        (Slot::Absent, Request::Toggle) => Action::Create,
        (Slot::Open, Request::Toggle | Request::Close) => Action::Destroy,
        (Slot::Open, Request::Key(event)) if event.kind == KeyKind::Escape => Action::Destroy,
        _ => Action::Ignore,
    }
}

/// Whether a window is open, and its fade.
pub struct ToplevelState {
    pub open: bool,
    pub opacity: Value,
    pub animations: AnimationManager,
}

impl Default for ToplevelState {
    fn default() -> Self {
        ToplevelState { open: false, opacity: value(0.0), animations: AnimationManager::default() }
    }
}

impl ToplevelState {
    pub fn open(&mut self) {
        self.open = true;
        self.animations.animate(self.opacity.get(), 1.0, OVERLAY_FADE_MS, Easing::EaseOutCubic, writer(&self.opacity), None, OVERLAY_FADE_OWNER);
    }

    pub fn close(&mut self) {
        self.animations.cancel_owner(OVERLAY_FADE_OWNER);
        self.open = false;
        self.opacity.set(0.0);
    }
}

pub struct ToplevelWindow {
    pub window: LayerWindow<Window>,
    pending: (i32, i32),
    pub state: ToplevelState,
}

impl ToplevelWindow {
    pub fn create(compositor: &CompositorState, xdg: &XdgShell, qh: &QueueHandle<State>, title: &str, app_id: &str, default_size: (i32, i32)) -> ToplevelWindow {
        let surface = compositor.create_surface(qh);
        let window = xdg.create_window(surface, WindowDecorations::ServerDefault, qh);
        window.set_title(title);
        window.set_app_id(app_id);
        window.commit();
        ToplevelWindow { window: LayerWindow::new(window, default_size.1), pending: default_size, state: ToplevelState::default() }
    }

    pub fn slot(&self) -> Slot {
        if self.state.open { Slot::Open } else { Slot::Pending }
    }

    /// Takes the configured size and creates or resizes the EGL window; true on the first configure.
    pub fn configure(&mut self, egl: &Egl, configure: &WindowConfigure) -> bool {
        let last = (configure.new_size.0.map_or(0, |v| v.get() as i32), configure.new_size.1.map_or(0, |v| v.get() as i32));
        let (width, height) = toplevel_size(last, self.pending);
        let first = self.window.egl_surface.is_none();
        self.window.height = height.max(1);
        self.window.configure(egl, width);
        first && self.window.egl_surface.is_some()
    }

    /// Fade, frame clock, `EGLSurface`, `wl_egl_window`, `xdg_toplevel`, `wl_surface`, in that order.
    pub fn destroy(mut self, egl: &Egl) {
        self.state.close();
        self.window.destroy(egl);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::base::input::{KeyEvent, KeyKind};

    #[test]
    fn toplevel_size_picks() {
        assert_eq!(toplevel_size((0, 0), (480, 600)), (480, 600), "a zero toplevel size uses the pending default");
        assert_eq!(toplevel_size((800, 0), (480, 600)), (800, 600), "each axis falls back on its own");
        assert_eq!(toplevel_size((800, 700), (480, 600)), (800, 700), "a non-zero size wins");
        assert_eq!(toplevel_size((800, 700), (480, 600)), (800, 700), "and sticks while the compositor repeats it");
    }

    fn key(kind: KeyKind) -> KeyEvent {
        KeyEvent { kind, text: String::new(), shift: false, alt: false, ctrl: false, base_sym: 0 }
    }

    #[test]
    fn toplevel_close_paths() {
        let escape = key(KeyKind::Escape);
        let other = key(KeyKind::Enter);
        assert_eq!(window_request(Slot::Absent, Request::Toggle), Action::Create, "the verb creates a missing window");
        assert_eq!(window_request(Slot::Pending, Request::Toggle), Action::Ignore, "a verb before the first configure is ignored");
        assert_eq!(window_request(Slot::Absent, Request::Close), Action::Ignore);
        assert_eq!(window_request(Slot::Absent, Request::Key(&escape)), Action::Ignore);
        for request in [Request::Toggle, Request::Close, Request::Key(&escape)] {
            assert_eq!(window_request(Slot::Open, request), Action::Destroy, "the verb, close and Escape each close an open window");
        }
        assert_eq!(window_request(Slot::Open, Request::Key(&other)), Action::Ignore, "a window only sees Escape");

        let mut state = ToplevelState::default();
        state.open();
        assert!(state.open && state.animations.has_active(), "opening fades in");
        state.close();
        assert!(!state.open && !state.animations.has_active() && state.opacity.get() == 0.0, "closing clears the fade owner");
    }
}
