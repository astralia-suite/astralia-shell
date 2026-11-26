//! The host of an app-wide overlay: a full-output layer surface on the `Overlay` layer, bound to one
//! output at a time, that fades in and out and takes the keyboard and input only while open.

use std::cell::Cell;
use std::rc::Rc;
use std::time::Instant;

use smithay_client_toolkit::compositor::{CompositorState, Region};
use smithay_client_toolkit::shell::WaylandSurface;
use smithay_client_toolkit::shell::wlr_layer::{Anchor, KeyboardInteractivity, Layer, LayerShell};
use wayland_client::protocol::wl_output::WlOutput;
use wayland_client::protocol::wl_surface::WlSurface;
use wayland_client::{Proxy, QueueHandle};

use super::egl::Egl;
use super::layer_window::LayerWindow;
use crate::wayland::State;
use crate::base::animation::{AnimationManager, Easing, Value, value, writer};

pub const OVERLAY_FADE_MS: f32 = 220.0;
pub const OVERLAY_FADE_OWNER: u64 = 1;
#[cfg(test)]
pub const PANEL_HEIGHT_ANIM_OWNER: u64 = 2;

/// Whether an overlay is open, its fade and the output it is bound to, apart from its surface.
pub struct OverlayState<O> {
    /// True from the open until the closing fade ends, as long as the overlay takes input.
    pub open: bool,
    pub opacity: Value,
    pub animations: AnimationManager,
    pub output: Option<O>,
    release_due: Rc<Cell<bool>>,
}

impl<O> Default for OverlayState<O> {
    fn default() -> Self {
        OverlayState { open: false, opacity: value(0.0), animations: AnimationManager::default(), output: None, release_due: Rc::default() }
    }
}

impl<O: PartialEq> OverlayState<O> {
    /// Opens a closed overlay or starts closing an open one; true when it opens. A closing overlay
    /// keeps `open` until its fade ends (see `tick`).
    pub fn toggle(&mut self, fade: bool) -> bool {
        let opening = !self.open;
        self.open = true;
        self.release_due.set(false);
        if !fade {
            self.animations.cancel_owner(OVERLAY_FADE_OWNER);
            if opening {
                self.opacity.set(1.0);
            } else {
                self.release_due.set(true);
            }
            return opening;
        }
        let due = self.release_due.clone();
        let on_done = (!opening).then(|| Box::new(move |_: &mut AnimationManager| due.set(true)) as _);
        let target = if opening { 1.0 } else { 0.0 };
        self.animations.animate(self.opacity.get(), target, OVERLAY_FADE_MS, Easing::EaseOutCubic, writer(&self.opacity), on_done, OVERLAY_FADE_OWNER);
        opening
    }

    /// Advances the tweens; true when a close just finished, so the caller gives the input back.
    pub fn tick(&mut self, now: Instant) -> bool {
        self.animations.tick(now);
        if !self.release_due.replace(false) {
            return false;
        }
        self.open = false;
        true
    }

    /// Resets the overlay when `removed` is the output it is bound to; true when it was.
    pub fn release_output(&mut self, removed: &O) -> bool {
        if self.output.as_ref() != Some(removed) {
            return false;
        }
        self.open = false;
        self.opacity.set(0.0);
        self.animations = AnimationManager::default();
        self.release_due.set(false);
        self.output = None;
        true
    }
}

/// The animated height of a card that grows open and shrinks closed.
#[cfg(test)]
pub struct PanelHeightReveal {
    visible: Value,
    target: f32,
    closing: bool,
    closed: Rc<Cell<bool>>,
}

#[cfg(test)]
impl Default for PanelHeightReveal {
    fn default() -> Self {
        PanelHeightReveal { visible: value(-1.0), target: -1.0, closing: false, closed: Rc::default() }
    }
}

#[cfg(test)]
impl PanelHeightReveal {
    pub fn open(&mut self) {
        self.visible.set(-1.0);
        self.target = -1.0;
        self.closing = false;
        self.closed.set(false);
    }

    /// The height to show this frame, animating towards `target_h` when it moved.
    pub fn tick(&mut self, animations: &mut AnimationManager, target_h: f32) -> f32 {
        if self.closing {
            return self.visible.get();
        }
        if self.visible.get() < 0.0 {
            self.visible.set(0.0);
        } else if (target_h - self.target).abs() <= 0.5 {
            return self.visible.get();
        }
        self.target = target_h;
        animations.animate(self.visible.get(), target_h, OVERLAY_FADE_MS, Easing::EaseOutCubic, writer(&self.visible), None, PANEL_HEIGHT_ANIM_OWNER);
        self.visible.get()
    }

    /// Shrinks to nothing; `take_closed` reports the end.
    pub fn close(&mut self, animations: &mut AnimationManager) {
        self.closing = true;
        let closed = self.closed.clone();
        let on_done = Box::new(move |_: &mut AnimationManager| closed.set(true));
        animations.animate(self.visible.get(), 0.0, OVERLAY_FADE_MS, Easing::EaseOutCubic, writer(&self.visible), Some(on_done), PANEL_HEIGHT_ANIM_OWNER);
    }

    /// True once when a close finished; the reveal then starts over.
    pub fn take_closed(&mut self) -> bool {
        if !self.closed.replace(false) {
            return false;
        }
        self.open();
        true
    }
}

/// The whole surface takes input while open, nothing while closed.
pub fn set_input_region(surface: &WlSurface, compositor: &CompositorState, open: bool) {
    if open {
        surface.set_input_region(None);
        return;
    }
    match Region::new(compositor) {
        Ok(empty) => surface.set_input_region(Some(empty.wl_region())),
        Err(e) => ::log::error!("panel: no input region: {e}"),
    }
}

/// An app-wide overlay surface. It has no surface until first bound to an output, and records a
/// toggle that arrives before the first configure, applying it when the configure comes.
pub struct Overlay {
    namespace: &'static str,
    pub state: OverlayState<WlOutput>,
    window: Option<LayerWindow>,
    pending_fade: Option<bool>,
}

impl Overlay {
    pub fn new(namespace: &'static str) -> Overlay {
        Overlay { namespace, state: OverlayState::default(), window: None, pending_fade: None }
    }

    pub fn is_open(&self) -> bool {
        self.state.open
    }

    /// Open, or opening once the first configure arrives.
    pub fn is_open_or_pending(&self) -> bool {
        self.state.open || self.pending_fade.is_some()
    }

    pub fn opacity(&self) -> f32 {
        self.state.opacity.get()
    }

    /// Puts a closed overlay on `output`, destroying its surface on any other output first. An open
    /// overlay stays where it is.
    pub fn bind(&mut self, egl: &Egl, compositor: &CompositorState, layer_shell: &LayerShell, qh: &QueueHandle<State>, output: &WlOutput, output_name: &str) {
        if self.state.open || (self.window.is_some() && self.state.output.as_ref() == Some(output)) {
            return;
        }
        if self.window.is_some() {
            ::log::info!("panel: {} retargeting to '{output_name}'", self.namespace);
        }
        self.destroy_surface(egl);
        let surface = compositor.create_surface(qh);
        let layer = layer_shell.create_layer_surface(qh, surface, Layer::Overlay, Some(self.namespace.to_string()), Some(output));
        layer.set_anchor(Anchor::TOP | Anchor::BOTTOM | Anchor::LEFT | Anchor::RIGHT);
        layer.set_exclusive_zone(-1);
        layer.set_keyboard_interactivity(KeyboardInteractivity::None);
        set_input_region(layer.wl_surface(), compositor, false);
        layer.commit();
        self.window = Some(LayerWindow::new(layer, 1));
        self.state = OverlayState { output: Some(output.clone()), ..OverlayState::default() };
    }

    /// Opens or starts closing; before the first configure the toggle waits for it. True when it opens.
    pub fn toggle(&mut self, compositor: &CompositorState, fade: bool) -> bool {
        let Some(window) = &self.window else { return false };
        if window.egl_surface.is_none() {
            let opening = self.pending_fade.is_none() && !self.state.open;
            self.pending_fade = if opening { Some(fade) } else { None };
            return opening;
        }
        let was_open = self.state.open;
        let opening = self.state.toggle(fade);
        if !was_open {
            self.set_input(compositor, true);
            ::log::info!("panel: {} acquired exclusive keyboard interactivity", self.namespace);
        }
        if self.state.tick(Instant::now()) {
            self.set_input(compositor, false);
        }
        opening
    }

    fn set_input(&self, compositor: &CompositorState, open: bool) {
        let Some(window) = &self.window else { return };
        let interactivity = if open { KeyboardInteractivity::Exclusive } else { KeyboardInteractivity::None };
        window.layer.set_keyboard_interactivity(interactivity);
        set_input_region(window.layer.wl_surface(), compositor, open);
        window.layer.wl_surface().commit();
        if !open {
            ::log::info!("panel: {} released exclusive keyboard interactivity", self.namespace);
        }
    }

    /// Advances the fade; a finished close gives the keyboard and input back. True while animating.
    pub fn tick(&mut self, compositor: &CompositorState, now: Instant) -> bool {
        if self.state.tick(now) {
            self.set_input(compositor, false);
        }
        self.state.animations.has_active()
    }

    /// A configure; returns whether a toggle queued before it opened the overlay now.
    pub fn configure(&mut self, egl: &Egl, compositor: &CompositorState, width: i32, height: i32) -> bool {
        let Some(window) = &mut self.window else { return false };
        window.set_height(height);
        window.configure(egl, width);
        match self.pending_fade.take() {
            Some(fade) => self.toggle(compositor, fade),
            None => false,
        }
    }

    /// Gives the keyboard and input back at once, for an overlay that plays its own close.
    pub fn release(&mut self, compositor: &CompositorState) {
        if !self.state.open {
            return;
        }
        self.state.open = false;
        self.state.animations.cancel_owner(OVERLAY_FADE_OWNER);
        self.set_input(compositor, false);
    }

    pub fn width(&self) -> i32 {
        self.window.as_ref().map_or(0, |w| w.width)
    }

    pub fn height(&self) -> i32 {
        self.window.as_ref().map_or(0, |w| w.height)
    }

    pub fn set_scale(&mut self, scale: i32) {
        if let Some(window) = &mut self.window {
            window.set_scale(scale);
        }
    }

    /// Asks for a frame while open or while `keep` holds (a close still drawing, a surface to clear);
    /// true when the caller draws now.
    pub fn request_frame(&mut self, keep: bool, qh: &QueueHandle<State>) -> bool {
        (self.state.open || keep) && self.window.as_mut().is_some_and(|w| w.request_frame(qh))
    }

    /// Frame callback done; true when a draw is due.
    pub fn frame_done(&mut self, qh: &QueueHandle<State>) -> bool {
        self.window.as_mut().is_some_and(|w| w.frame_done(qh))
    }

    /// See `LayerWindow::frame_time`; now while there is no window.
    pub fn frame_time(&self) -> Instant {
        self.window.as_ref().map_or_else(Instant::now, LayerWindow::frame_time)
    }

    pub fn window_mut(&mut self) -> Option<&mut LayerWindow> {
        self.window.as_mut()
    }

    pub fn owns(&self, surface: &WlSurface) -> bool {
        self.window.as_ref().is_some_and(|w| w.owns(surface))
    }

    pub fn surface(&self) -> Option<WlSurface> {
        self.window.as_ref().map(|w| w.layer.wl_surface().clone())
    }

    pub fn surface_id(&self) -> Option<wayland_client::backend::ObjectId> {
        self.window.as_ref().map(|w| w.layer.wl_surface().id())
    }

    /// When bound to `removed`, destroys the surface and resets the overlay; returns the destroyed
    /// surface so input focus on it can be dropped.
    pub fn release_output(&mut self, egl: &Egl, removed: &WlOutput) -> Option<wayland_client::backend::ObjectId> {
        if !self.state.release_output(removed) {
            return None;
        }
        self.pending_fade = None;
        let id = self.surface_id();
        self.destroy_surface(egl);
        id
    }

    pub fn destroy_surface(&mut self, egl: &Egl) {
        if let Some(window) = self.window.take() {
            window.destroy(egl);
        }
    }

    pub fn destroy(mut self, egl: &Egl) {
        self.destroy_surface(egl);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;

    #[test]
    fn overlay_release_on_output_removed() {
        let mut state: OverlayState<u32> = OverlayState { output: Some(1), ..OverlayState::default() };
        assert!(state.toggle(false), "the first toggle opens");
        assert!(state.open && state.opacity.get() == 1.0);
        assert!(!state.release_output(&2), "removing another output changes nothing");
        assert!(state.open && state.output == Some(1));
        assert!(state.release_output(&1), "removing the bound output releases the overlay");
        assert!(!state.open && state.opacity.get() == 0.0 && state.output.is_none() && !state.animations.has_active());
        assert!(!state.release_output(&1), "an unbound overlay has nothing to release");
    }

    #[test]
    fn overlay_fade_releases_after_close() {
        let mut state: OverlayState<u32> = OverlayState::default();
        let t0 = Instant::now();
        assert!(state.toggle(true));
        assert!(state.open, "an opening overlay takes input at once");
        state.tick(t0 + Duration::from_millis(500));
        assert_eq!(state.opacity.get(), 1.0);
        assert!(!state.toggle(true), "the second toggle closes");
        assert!(state.open, "a closing overlay keeps input until the fade ends");
        assert!(!state.tick(Instant::now() + Duration::from_millis(10)), "no release mid-fade");
        assert!(state.tick(Instant::now() + Duration::from_millis(500)), "the fade's end releases it");
        assert!(state.opacity.get() == 0.0);
    }

    #[test]
    fn reveal_tick() {
        let mut anims = AnimationManager::default();
        let mut r = PanelHeightReveal::default();
        r.open();
        let t0 = Instant::now();
        assert_eq!(r.tick(&mut anims, 100.0), 0.0, "the first tick starts from nothing");
        anims.tick(t0 + Duration::from_millis(110));
        let mid = r.tick(&mut anims, 100.0);
        assert!(mid > 0.0 && mid < 100.0, "half way the height is partly revealed: {mid}");
        anims.tick(t0 + Duration::from_millis(500));
        assert_eq!(r.tick(&mut anims, 100.0), 100.0);
        assert_eq!(r.tick(&mut anims, 100.3), 100.0, "a change under half a pixel is ignored");
        r.tick(&mut anims, 200.0);
        anims.tick(Instant::now() + Duration::from_millis(500));
        assert_eq!(r.tick(&mut anims, 200.0), 200.0, "a new target animates to it");
        r.close(&mut anims);
        assert_eq!(r.tick(&mut anims, 300.0), 200.0, "a closing reveal ignores new targets");
        assert!(!r.take_closed());
        anims.tick(Instant::now() + Duration::from_millis(500));
        assert!(r.take_closed(), "the close finishes once");
        assert!(!r.take_closed());
        assert_eq!(r.tick(&mut anims, 50.0), 0.0, "and the next tick starts over");
    }
}
