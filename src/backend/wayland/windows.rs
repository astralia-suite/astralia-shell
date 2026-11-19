//! The toplevel windows: one slot per kind, opened by its verb, tiled by the compositor.

use smithay_client_toolkit::shell::WaylandSurface;
use smithay_client_toolkit::shell::xdg::window::{Window, WindowConfigure, WindowHandler};
use wayland_client::protocol::wl_surface::WlSurface;
use wayland_client::{Connection, QueueHandle};

use super::State;
use super::render::toplevel_window::{Action, Request, Slot, ToplevelWindow, window_request};
use crate::config::{bamboo_config, rain_config, visualizer_config};
use crate::modules::bamboo::wayland::bamboo::Bamboo;
use crate::modules::rain::wayland::rain::Rain;
use crate::modules::visualizer::wayland::shaders::Colors;
use crate::modules::visualizer::wayland::visualizer::Visualizer;
use crate::render::theme::palette;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WindowId {
    Rain,
    Bamboo,
    Visualizer,
}

impl WindowId {
    pub const ALL: [WindowId; 3] = [WindowId::Rain, WindowId::Bamboo, WindowId::Visualizer];

    fn title(self) -> &'static str {
        match self {
            WindowId::Rain => "Rain",
            WindowId::Bamboo => "Bamboo",
            WindowId::Visualizer => "Visualizer",
        }
    }

    fn app_id(self) -> &'static str {
        match self {
            WindowId::Rain => "astralia-shell-rain",
            WindowId::Bamboo => "astralia-shell-bamboo",
            WindowId::Visualizer => "astralia-shell-visualizer",
        }
    }

    fn default_size(self) -> (i32, i32) {
        match self {
            WindowId::Rain => (rain_config::DEFAULT_WINDOW_WIDTH, rain_config::DEFAULT_WINDOW_HEIGHT),
            WindowId::Bamboo => (bamboo_config::DEFAULT_WINDOW_WIDTH, bamboo_config::DEFAULT_WINDOW_HEIGHT),
            WindowId::Visualizer => (visualizer_config::DEFAULT_WINDOW, visualizer_config::DEFAULT_WINDOW),
        }
    }
}

#[derive(Default)]
pub struct Windows {
    slots: [Option<ToplevelWindow>; 3],
    /// Present only with the rain capability.
    pub rain: Option<Rain>,
    pub bamboo: Bamboo,
    pub visualizer: Visualizer,
}

/// The palette colours the visualizer's render thread draws with.
fn visualizer_colors() -> Colors {
    let p = palette();
    Colors { accent: [p.accent.r, p.accent.g, p.accent.b], backdrop: p.window_backdrop.rgba() }
}

impl Windows {
    pub fn get(&self, id: WindowId) -> Option<&ToplevelWindow> {
        self.slots[id as usize].as_ref()
    }

    pub fn get_mut(&mut self, id: WindowId) -> Option<&mut ToplevelWindow> {
        self.slots[id as usize].as_mut()
    }

    pub fn is_open(&self, id: WindowId) -> bool {
        self.get(id).is_some_and(|w| w.state.open)
    }

    fn slot(&self, id: WindowId) -> Slot {
        self.get(id).map_or(Slot::Absent, ToplevelWindow::slot)
    }

    pub fn owner_of(&self, surface: &WlSurface) -> Option<WindowId> {
        WindowId::ALL.into_iter().find(|&id| self.get(id).is_some_and(|w| w.window.owns(surface)))
    }

    fn owner_of_window(&self, window: &Window) -> Option<WindowId> {
        self.owner_of(window.wl_surface())
    }
}

impl State {
    pub fn window_request(&mut self, id: WindowId, request: Request) {
        match window_request(self.windows.slot(id), request) {
            Action::Create => self.create_window(id),
            Action::Destroy => self.close_window(id),
            Action::Ignore => {}
        }
    }

    fn create_window(&mut self, id: WindowId) {
        let Some(xdg) = &self.xdg_shell else {
            ::log::warn!("{}: no xdg_wm_base, cannot open the window", id.title());
            return;
        };
        let window = ToplevelWindow::create(&self.compositor, xdg, &self.qh, id.title(), id.app_id(),id.default_size());
        self.windows.slots[id as usize] = Some(window);
    }

    /// Stops the window's content, then destroys it in the surface teardown order.
    pub fn close_window(&mut self, id: WindowId) {
        let Some(window) = self.windows.slots[id as usize].take() else { return };
        self.window_closing(id);
        if self.keyboard_focus.as_ref().is_some_and(|s| window.window.owns(s)) {
            self.keyboard_focus = None;
        }
        window.destroy(&self.egl);
        self.egl.rest();
    }

    /// The visualizer's render thread presents on its own; the loop never draws that window.
    pub fn request_window_frame(&mut self, id: WindowId) {
        if id == WindowId::Visualizer {
            return;
        }
        let Some(window) = self.windows.get_mut(id) else { return };
        if window.window.request_frame(&self.qh) {
            self.draw_window(id);
        }
    }

    pub fn window_frame_done(&mut self, id: WindowId) {
        let Some(window) = self.windows.get_mut(id) else { return };
        if window.window.frame_done(&self.qh) {
            self.draw_window(id);
        }
    }

    /// The visualizer keeps buffer scale 1: its render thread sizes its viewport from the logical size.
    pub fn window_scale_changed(&mut self, id: WindowId, scale: i32) {
        if id == WindowId::Visualizer {
            return;
        }
        if let Some(window) = self.windows.get_mut(id) {
            window.window.set_scale(scale);
        }
        self.request_window_frame(id);
    }

    fn draw_window(&mut self, id: WindowId) {
        let Some(window) = self.windows.get_mut(id) else { return };
        window.state.animations.tick(std::time::Instant::now());
        if !window.window.begin(&self.egl, &mut self.renderer) {
            return;
        }
        let opacity = window.state.opacity.get();
        let size = (window.window.width, window.window.height);
        self.renderer.set_opacity(opacity);
        self.paint_window(id, size);
        self.renderer.set_opacity(1.0);
        let Some(window) = self.windows.get_mut(id) else { return };
        window.window.end(&self.egl);
        if window.state.open || window.state.animations.has_active() {
            self.request_window_frame(id);
        }
    }

    /// Draws the window's content into the current frame.
    fn paint_window(&mut self, id: WindowId, size: (i32, i32)) {
        match id {
            WindowId::Rain => {
                if let Some(rain) = &mut self.windows.rain {
                    rain.paint(&self.renderer, size);
                }
            }
            WindowId::Bamboo => self.windows.bamboo.paint(&self.renderer, size),
            WindowId::Visualizer => {}
        }
    }

    /// The window opened: its content starts.
    fn window_opened(&mut self, id: WindowId) {
        match id {
            WindowId::Rain => {
                if let Some(rain) = &mut self.windows.rain {
                    rain.opened();
                }
            }
            WindowId::Bamboo => self.windows.bamboo.opened(),
            WindowId::Visualizer => self.start_visualizer(),
        }
    }

    /// Releases the window's surface from the loop, starts capture and hands the surface to the render thread.
    fn start_visualizer(&mut self) {
        let Some(window) = self.windows.get(WindowId::Visualizer) else { return };
        let (Some(surface), size) = (window.window.egl_surface, (window.window.width, window.window.height)) else { return };
        self.egl.rest();
        let cfg = self.config();
        let visualizer = &mut self.windows.visualizer;
        visualizer.capture.start();
        visualizer.start_thread(&self.egl, surface, size, &cfg.visualizer, visualizer_colors());
    }

    /// The window is about to be destroyed: its content stops.
    fn window_closing(&mut self, id: WindowId) {
        match id {
            WindowId::Rain => {
                if let Some(rain) = &mut self.windows.rain {
                    rain.closed(&self.renderer);
                }
            }
            WindowId::Bamboo => self.windows.bamboo.closed(&self.renderer),
            WindowId::Visualizer => self.windows.visualizer.shutdown(&self.egl),
        }
    }

    /// New settings and palette reach the windows; a changed rain mode rebuilds an open window.
    pub fn windows_apply_config(&mut self, cfg: &crate::config::settings::Config) {
        self.windows.visualizer.apply_params(&cfg.visualizer, visualizer_colors());
        let open = self.windows.is_open(WindowId::Rain);
        if self.windows.rain.as_mut().is_some_and(|rain| rain.apply_params(&cfg.rain, open)) {
            self.request_window_frame(WindowId::Rain);
        }
    }
}

impl WindowHandler for State {
    fn request_close(&mut self, _: &Connection, _: &QueueHandle<Self>, window: &Window) {
        if let Some(id) = self.windows.owner_of_window(window) {
            self.window_request(id, Request::Close);
        }
    }

    fn configure(&mut self, _: &Connection, _: &QueueHandle<Self>, window: &Window, configure: WindowConfigure, _: u32) {
        let Some(id) = self.windows.owner_of_window(window) else { return };
        let Some(toplevel) = self.windows.get_mut(id) else { return };
        if toplevel.configure(&self.egl, &configure) {
            toplevel.state.open();
            self.window_opened(id);
        } else if id == WindowId::Visualizer {
            let size = (toplevel.window.width, toplevel.window.height);
            self.windows.visualizer.resize(size);
        }
        self.request_window_frame(id);
    }
}
