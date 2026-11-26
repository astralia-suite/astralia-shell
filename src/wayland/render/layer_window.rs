//! A role surface (a layer surface by default) with its `wl_egl_window`, `EGLSurface` and frame clock.

use khronos_egl as egl;
use smithay_client_toolkit::shell::WaylandSurface;
use smithay_client_toolkit::shell::wlr_layer::LayerSurface;
use wayland_client::protocol::wl_surface::WlSurface;
use wayland_client::{Proxy, QueueHandle};
use wayland_egl::WlEglSurface;

use super::buffer_size;
use super::egl::Egl;
use super::renderer::Renderer;
use crate::wayland::State;
use crate::wayland::frame::{FrameAction, FrameClock};
use crate::wayland::presentation::{self, SharedClock};
use std::time::Instant;

/// Dropping the role destroys the role object, then the `wl_surface`.
pub struct LayerWindow<R: WaylandSurface = LayerSurface> {
    pub layer: R,
    pub egl_window: Option<WlEglSurface>,
    pub egl_surface: Option<egl::Surface>,
    pub frame: FrameClock,
    pub width: i32,
    pub height: i32,
    pub scale: i32,
    /// Buffer scale last sent to the compositor; a new one is sent with the next drawn buffer.
    applied_scale: i32,
    /// When this surface's frames reach the screen; see `frame_time`.
    present: SharedClock,
    /// The previous `frame_time`, for `ASTRALIA_FRAME_DEBUG`.
    last_frame_time: std::cell::Cell<Option<Instant>>,
}

impl<R: WaylandSurface> LayerWindow<R> {
    pub fn new(layer: R, height: i32) -> LayerWindow<R> {
        LayerWindow { layer, egl_window: None, egl_surface: None, frame: FrameClock::default(), width: 0, height, scale: 1, applied_scale: 1, present: SharedClock::default(), last_frame_time: std::cell::Cell::new(None) }
    }

    /// First configure creates the `wl_egl_window` and `EGLSurface`; later ones resize.
    pub fn configure(&mut self, egl: &Egl, width: i32) {
        if width <= 0 {
            return;
        }
        self.width = width;
        let (w, h) = buffer_size(self.width, self.height, self.scale);
        if let Some(window) = &self.egl_window {
            window.resize(w, h, 0, 0);
            return;
        }
        let window = match WlEglSurface::new(self.layer.wl_surface().id(), w, h) {
            Ok(window) => window,
            Err(e) => {
                ::log::error!("egl: wl_egl_window_create failed: {e}");
                return;
            }
        };
        // SAFETY: `window` is a live `wl_egl_window`, kept alive until after the `EGLSurface` is destroyed.
        match unsafe { egl.egl.create_window_surface(egl.display, egl.config, window.ptr() as egl::NativeWindowType, None) } {
            Ok(surface) => self.egl_surface = Some(surface),
            Err(e) => ::log::error!("egl: eglCreateWindowSurface failed 0x{:04x}", e.native()),
        }
        self.egl_window = Some(window);
    }

    /// A new logical height; the buffer follows at once, the surface with the next commit.
    pub fn set_height(&mut self, height: i32) {
        self.height = height.max(1);
        if let Some(window) = &self.egl_window {
            let (w, h) = buffer_size(self.width, self.height, self.scale);
            window.resize(w, h, 0, 0);
        }
    }

    pub fn set_scale(&mut self, scale: i32) {
        self.scale = scale.max(1);
        if let Some(window) = &self.egl_window {
            let (w, h) = buffer_size(self.width, self.height, self.scale);
            window.resize(w, h, 0, 0);
        }
    }

    fn arm(&self, qh: &QueueHandle<State>) {
        let surface = self.layer.wl_surface();
        surface.frame(qh, surface.clone());
        presentation::request(surface, qh, &self.present);
    }

    /// The time the frame being drawn now is for: on the compositor's refresh grid when it reports
    /// presentation times, else now. Animations tick at it so every frame advances them evenly.
    pub fn frame_time(&self) -> Instant {
        let now = Instant::now();
        let time = self.present.lock().map_or(now, |mut clock| clock.target(now));
        if presentation::debug()
            && let Some(previous) = self.last_frame_time.replace(Some(time))
        {
            ::log::info!(
                "frame: stride {:.2} ms, ahead {:.2} ms, surface {}",
                time.saturating_duration_since(previous).as_secs_f64() * 1000.0,
                time.saturating_duration_since(now).as_secs_f64() * 1000.0,
                self.layer.wl_surface().id()
            );
        }
        time
    }

    /// Asks for a frame; true when the caller draws now.
    pub fn request_frame(&mut self, qh: &QueueHandle<State>) -> bool {
        // Before the first configure nothing can commit, so an armed callback would never fire.
        if self.egl_surface.is_none() {
            return false;
        }
        match self.frame.request() {
            FrameAction::None => false,
            FrameAction::ArmAndDraw => {
                self.arm(qh);
                true
            }
            FrameAction::ArmAndCommit => {
                self.arm(qh);
                let surface = self.layer.wl_surface();
                surface.damage_buffer(0, 0, 1, 1);
                surface.commit();
                false
            }
        }
    }

    /// Frame callback done; true when a draw is due.
    pub fn frame_done(&mut self, qh: &QueueHandle<State>) -> bool {
        if self.frame.done() == FrameAction::ArmAndDraw {
            self.arm(qh);
            return true;
        }
        false
    }

    /// Makes the window current and starts a cleared frame; false skips the frame.
    pub fn begin(&mut self, egl: &Egl, renderer: &mut Renderer) -> bool {
        let Some(surface) = self.egl_surface else { return false };
        if self.width <= 0 || !egl.make_current(Some(surface)) {
            return false;
        }
        if self.applied_scale != self.scale {
            // Sent with the buffer drawn at this scale, so the committed size always divides by it.
            self.layer.wl_surface().set_buffer_scale(self.scale);
            self.applied_scale = self.scale;
        }
        renderer.begin_frame(self.width, self.height, self.scale);
        renderer.clear();
        true
    }

    /// Swaps, which commits the surface.
    pub fn end(&self, egl: &Egl) -> bool {
        let Some(surface) = self.egl_surface else { return false };
        if let Err(e) = egl.egl.swap_buffers(egl.display, surface) {
            ::log::error!("egl: eglSwapBuffers failed 0x{:04x}", e.native());
            return false;
        }
        true
    }

    pub fn owns(&self, surface: &WlSurface) -> bool {
        self.layer.wl_surface() == surface
    }

    /// Frame clock, `EGLSurface`, `wl_egl_window`, layer surface, `wl_surface`, in that order.
    pub fn destroy(mut self, egl: &Egl) {
        self.frame.drop_callback();
        if let Some(surface) = self.egl_surface.take() {
            egl.release_if_current(surface);
            if let Err(e) = egl.egl.destroy_surface(egl.display, surface) {
                ::log::error!("egl: eglDestroySurface failed 0x{:04x}", e.native());
            }
        }
        drop(self.egl_window.take());
        // sctk destroys the layer surface role, then the `wl_surface`.
        drop(self.layer);
    }
}
