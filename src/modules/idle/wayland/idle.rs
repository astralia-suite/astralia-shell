//! The per-monitor idle overlay: the dimmed wallpaper (ambient) and the bouncing-logo screensaver.

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
use crate::base::animation::{AnimationManager, Easing, Value, value, writer};
use crate::config::idle_config::{AMBIENT_FADE_OWNER, IDLE_FADE_MS, IDLE_LAYER_NAMESPACE, LOGO_ASSET, LOGO_SIZE, LOGO_SPEED, SCREENSAVER_FADE_OWNER};
use crate::modules::idle::bounce::{Bounce, bounce_step};
use crate::modules::idle::clock::Verdict;
use crate::modules::wallpaper::wayland::wallpaper::Wallpaper;
use crate::render::canvas::{Canvas, NO_IMAGE};
use crate::render::geometry::Rect;
use crate::render::tokens::{Color, WHITE, with_alpha};

/// The two fades; the ambient overlay hides while the screensaver shows.
pub struct IdleFade {
    pub animations: AnimationManager,
    pub ambient_opacity: Value,
    pub screensaver_opacity: Value,
    ambient_active: bool,
    pub screensaver_active: bool,
}

impl Default for IdleFade {
    fn default() -> Self {
        IdleFade { animations: AnimationManager::default(), ambient_opacity: value(0.0), screensaver_opacity: value(0.0), ambient_active: false, screensaver_active: false }
    }
}

impl IdleFade {
    /// Starts the fades the new state needs; true when one started.
    pub fn set_active(&mut self, ambient: bool, screensaver: bool) -> bool {
        let mut changed = false;
        let ambient_shown = ambient && !screensaver;
        if ambient_shown != self.ambient_active {
            self.ambient_active = ambient_shown;
            let target = if ambient_shown { 1.0 } else { 0.0 };
            self.animations.animate(self.ambient_opacity.get(), target, IDLE_FADE_MS, Easing::EaseOutCubic, writer(&self.ambient_opacity), None, AMBIENT_FADE_OWNER);
            changed = true;
        }
        if screensaver != self.screensaver_active {
            self.screensaver_active = screensaver;
            let target = if screensaver { 1.0 } else { 0.0 };
            self.animations.animate(self.screensaver_opacity.get(), target, IDLE_FADE_MS, Easing::EaseOutCubic, writer(&self.screensaver_opacity), None, SCREENSAVER_FADE_OWNER);
            changed = true;
        }
        changed
    }
}

/// Turns the screensaver state into pause and resume edges for the wallpaper.
#[derive(Default)]
pub struct ScreensaverEdge {
    was: bool,
}

impl ScreensaverEdge {
    /// `Some(paused)` when the screensaver just started or ended.
    pub fn update(&mut self, screensaver: bool) -> Option<bool> {
        (screensaver != self.was).then(|| {
            self.was = screensaver;
            screensaver
        })
    }
}

pub struct IdleOverlay {
    pub window: LayerWindow,
    canvas: GlCanvas,
    pub fade: IdleFade,
    edge: ScreensaverEdge,
    logo_path: String,
    bounce: Bounce,
    last_tick: Instant,
}

impl IdleOverlay {
    pub fn new(compositor: &CompositorState, layer_shell: &LayerShell, qh: &QueueHandle<State>, output: &WlOutput) -> IdleOverlay {
        let surface = compositor.create_surface(qh);
        let layer = layer_shell.create_layer_surface(qh, surface, Layer::Overlay, Some(IDLE_LAYER_NAMESPACE), Some(output));
        layer.set_anchor(Anchor::TOP | Anchor::BOTTOM | Anchor::LEFT | Anchor::RIGHT);
        layer.set_exclusive_zone(-1);
        layer.set_keyboard_interactivity(KeyboardInteractivity::None);
        match Region::new(compositor) {
            Ok(empty) => layer.wl_surface().set_input_region(Some(empty.wl_region())),
            Err(e) => ::log::error!("idle-overlay: no input region: {e}"),
        }
        layer.commit();
        IdleOverlay {
            window: LayerWindow::new(layer, 0),
            canvas: GlCanvas::new(),
            fade: IdleFade::default(),
            edge: ScreensaverEdge::default(),
            logo_path: String::new(),
            bounce: Bounce { x: 40.0, y: 40.0, vx: LOGO_SPEED, vy: LOGO_SPEED * 0.75 },
            last_tick: Instant::now(),
        }
    }

    pub fn owns(&self, surface: &WlSurface) -> bool {
        self.window.owns(surface)
    }

    pub fn surface_id(&self) -> wayland_client::backend::ObjectId {
        self.window.layer.wl_surface().id()
    }

    pub fn configure(&mut self, egl: &Egl, width: i32, height: i32) {
        self.window.height = height.max(1);
        self.window.configure(egl, width);
    }

    /// Applies the verdict; returns whether a frame is due and the wallpaper pause edge.
    pub fn update(&mut self, verdict: Verdict, logo_path: &str) -> (bool, Option<bool>) {
        if self.logo_path != logo_path {
            self.logo_path = logo_path.to_string();
        }
        let due = self.fade.set_active(verdict.ambient, verdict.screensaver);
        (due, self.edge.update(verdict.screensaver))
    }

    /// Paints one frame; true while a fade runs or the logo bounces.
    pub fn draw(&mut self, egl: &Egl, renderer: &mut Renderer, wallpaper: Option<&Wallpaper>) -> bool {
        if !self.window.begin(egl, renderer) {
            return false;
        }
        let now = Instant::now();
        self.fade.animations.tick(now);
        let dt = now.saturating_duration_since(self.last_tick).as_secs_f32();
        self.last_tick = now;
        let (w, h) = (self.window.width as f32, self.window.height as f32);
        if self.fade.screensaver_active && dt > 0.0 && dt <= 0.5 {
            bounce_step(&mut self.bounce, dt, w, h, LOGO_SIZE);
        }
        let ambient = self.fade.ambient_opacity.get();
        if ambient > 0.0
            && let Some(wallpaper) = wallpaper
        {
            renderer.set_opacity(ambient);
            wallpaper.draw_columns(renderer, self.window.width, self.window.height);
            renderer.set_opacity(1.0);
        }
        let saver = self.fade.screensaver_opacity.get();
        if saver > 0.0 {
            let scale = self.window.scale;
            let mut frame = self.canvas.begin(renderer, scale);
            frame.rect(Rect { x: 0.0, y: 0.0, w, h }, Color { r: 0.0, g: 0.0, b: 0.0, a: saver });
            let asset = if self.logo_path.is_empty() { LOGO_ASSET } else { &self.logo_path };
            let logo = frame.image(asset, LOGO_SIZE as i32 * scale);
            if logo != NO_IMAGE {
                let extent = frame.image_size(logo);
                let fit = (LOGO_SIZE / extent.w).min(LOGO_SIZE / extent.h);
                let (lw, lh) = (extent.w * fit, extent.h * fit);
                let r = Rect { x: self.bounce.x + (LOGO_SIZE - lw) / 2.0, y: self.bounce.y + (LOGO_SIZE - lh) / 2.0, w: lw, h: lh };
                frame.draw_image(logo, r, with_alpha(WHITE, saver));
            }
            frame.flush();
        }
        self.window.end(egl);
        self.fade.animations.has_active() || self.fade.screensaver_active
    }

    pub fn destroy(self, egl: &Egl, renderer: &Renderer) {
        if egl.make_current(None) {
            self.canvas.destroy(renderer);
        }
        self.window.destroy(egl);
    }
}

#[cfg(test)]
mod tests {
    use super::{IdleFade, ScreensaverEdge};
    use std::time::{Duration, Instant};

    fn settle(fade: &mut IdleFade) -> (f32, f32) {
        fade.animations.tick(Instant::now() + Duration::from_secs(5));
        (fade.ambient_opacity.get(), fade.screensaver_opacity.get())
    }

    #[test]
    fn idle_overlay_set_active() {
        let mut fade = IdleFade::default();
        assert!(!fade.set_active(false, false), "nothing to change");
        assert!(fade.set_active(true, false));
        assert_eq!(settle(&mut fade), (1.0, 0.0), "ambient alone fades ambient in");
        assert!(fade.set_active(true, true));
        assert_eq!(settle(&mut fade), (0.0, 1.0), "the screensaver hides the ambient overlay and fades in");
        assert!(!fade.set_active(true, true), "repeating a state changes nothing");
        assert!(!fade.animations.has_active());
        assert!(fade.set_active(false, false));
        assert_eq!(settle(&mut fade), (0.0, 0.0), "activity fades both out");
    }

    #[test]
    fn idle_pause_hook() {
        let mut edge = ScreensaverEdge::default();
        assert_eq!(edge.update(false), None, "ambient alone never pauses");
        assert_eq!(edge.update(true), Some(true), "the screensaver starting pauses");
        assert_eq!(edge.update(true), None, "once per edge");
        assert_eq!(edge.update(false), Some(false), "and resumes when it ends");
        assert_eq!(edge.update(false), None);
    }
}
