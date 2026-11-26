//! One output's bar: a top layer surface drawing the model through `GlCanvas`, with autohide.

use std::cell::Cell;
use std::rc::Rc;
use std::time::Instant;

use smithay_client_toolkit::compositor::CompositorState as WlCompositorState;
use smithay_client_toolkit::shell::WaylandSurface;
use smithay_client_toolkit::shell::wlr_layer::{Anchor, KeyboardInteractivity, Layer, LayerShell};
use wayland_client::backend::ObjectId;
use wayland_client::protocol::wl_output::WlOutput;
use wayland_client::{Proxy, QueueHandle};

use super::panel_surface::PanelSurface;
use crate::wayland::monitor::Redraw;
use crate::wayland::{Ctx, State};
use crate::modules::bar::model::items::panel_for;
use crate::wayland::render::egl::Egl;
use crate::wayland::render::gl_canvas::GlCanvas;
use crate::wayland::render::layer_window::LayerWindow;
use crate::wayland::render::renderer::Renderer;
use crate::base::animation::{self, AnimationManager, Easing, Value};
use crate::config::bar_layout;
use crate::config::settings::{BarStyle, Config, autohide_enabled_for, expand_widgets_for};
use crate::modules::bar::model::{ActionKind, Item, Model, Sources};
use crate::modules::bar::panel::content::PanelSources;
use crate::modules::bar::paint::frame::{frame_base, frame_overlay};
use crate::modules::bar::paint::style::{Geometry, autohide_geometry, style_spec};
use crate::modules::bar::paint::view::paint;
use crate::render::canvas::{Canvas, GroupOptions};
use crate::render::geometry::Rect;
use crate::service::audio::AudioService;
use crate::service::brightness::BrightnessService;
use crate::service::compositor::{Compositor, CompositorState};

const AUTOHIDE_REVEAL_MS: f32 = 150.0;
const AUTOHIDE_HIDE_MS: f32 = 150.0;
const AUTOHIDE_OWNER: u64 = 1000;
const VOLUME_STEP: i32 = 1;
const VOLUME_MAX: i32 = 150;
const BRIGHTNESS_STEP: i32 = 1;

/// The layer surface's size, margins and reserved zone for one config.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SurfaceGeometry {
    pub geometry: Geometry,
    pub side_margin: i32,
}

/// Okinami hugs the screen corners unless it autohides; the compositor's outer gap replaces the side margin.
pub fn geometry_for(cfg: &Config, output: &str, collapsed: bool, compositor: &CompositorState) -> SurfaceGeometry {
    let autohide = autohide_enabled_for(cfg, output);
    let style = style_spec(cfg.bar_style, true);
    let hug = if cfg.bar_style == BarStyle::Okinami && !autohide { compositor.hug_radius_px } else { 0 };
    let side_margin = if style.side_margin == 0 { 0 } else { compositor.gaps_out_px.unwrap_or(style.side_margin) };
    SurfaceGeometry { geometry: autohide_geometry(autohide, collapsed && autohide, bar_layout::HEIGHT, style.top_margin, hug), side_margin }
}

struct AutoHide {
    enabled: bool,
    hidden: bool,
    collapsed: bool,
    opacity: Value,
    /// Set when the hide fade ends, so the next draw collapses the surface.
    faded: Rc<Cell<bool>>,
}

/// What the bar needs from the shell to act on a press or a wheel.
pub struct Controls {
    pub compositor: Rc<dyn Compositor>,
    pub audio: Option<Rc<AudioService>>,
    pub brightness: Option<Rc<BrightnessService>>,
}

/// Where panel cards start for a style: below the bar and the panel gap.
pub fn card_top(style: BarStyle) -> f32 {
    (bar_layout::HEIGHT + style_spec(style, true).top_margin + bar_layout::PANEL_GAP) as f32
}

/// What a left click on the bar asks the shell to do.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BarPress {
    Nothing,
    Panel(Item),
    Logout,
    Overview,
}

pub struct Bar {
    pub window: LayerWindow,
    pub panels: PanelSurface,
    canvas: GlCanvas,
    animations: AnimationManager,
    autohide: AutoHide,
    model: Model,
    controls: Controls,
    output: String,
    style: BarStyle,
    applied: SurfaceGeometry,
    pointer: Option<(f64, f64)>,
}

impl Bar {
    #[allow(clippy::too_many_arguments)]
    pub fn new(compositor: &WlCompositorState, layer_shell: &LayerShell, qh: &QueueHandle<State>, output: &WlOutput, name: &str, cfg: &Config, sources: Sources, controls: Controls, panel_sources: &PanelSources, redraw: &Redraw) -> Bar {
        let applied = geometry_for(cfg, name, false, &controls.compositor.state());
        let surface = compositor.create_surface(qh);
        let layer = layer_shell.create_layer_surface(qh, surface, Layer::Top, Some("astralia-shell"), Some(output));
        layer.set_anchor(Anchor::TOP | Anchor::LEFT | Anchor::RIGHT);
        layer.set_keyboard_interactivity(KeyboardInteractivity::None);
        let g = applied.geometry;
        layer.set_size(0, g.height as u32);
        layer.set_margin(g.margin_top, applied.side_margin, 0, applied.side_margin);
        layer.set_exclusive_zone(g.exclusive_zone);
        layer.commit();
        let enabled = autohide_enabled_for(cfg, name);
        Bar {
            window: LayerWindow::new(layer, g.height),
            panels: PanelSurface::new(compositor, layer_shell, qh, output, redraw, card_top(cfg.bar_style), panel_sources),
            canvas: GlCanvas::new(),
            animations: AnimationManager::default(),
            autohide: AutoHide { enabled, hidden: false, collapsed: false, opacity: animation::value(1.0), faded: Rc::default() },
            model: Model::new(sources, Instant::now()),
            controls,
            output: name.to_string(),
            style: cfg.bar_style,
            applied,
            pointer: None,
        }
    }

    /// Finished image decodes ask for a repaint through `notify`.
    pub fn set_on_image_ready(&mut self, notify: Rc<dyn Fn()>) {
        self.canvas.set_on_image_ready(notify);
    }

    /// The first configure creates the EGL surface; an autohiding bar then starts hidden.
    pub fn configure(&mut self, egl: &Egl, width: i32) {
        let first = self.window.egl_surface.is_none();
        self.window.configure(egl, width);
        if first && self.window.egl_surface.is_some() && self.autohide.enabled {
            self.autohide.hidden = true;
            self.autohide.collapsed = true;
            self.autohide.opacity.set(0.0);
        }
    }

    fn content_offset(&self, cfg_style: BarStyle) -> f32 {
        if self.autohide.enabled { style_spec(cfg_style, true).top_margin as f32 } else { 0.0 }
    }

    fn apply_geometry(&mut self, cfg: &Config) {
        let g = geometry_for(cfg, &self.output, self.autohide.collapsed, &self.controls.compositor.state());
        let layer = &self.window.layer;
        layer.set_size(0, g.geometry.height as u32);
        layer.set_margin(g.geometry.margin_top, g.side_margin, 0, g.side_margin);
        layer.set_exclusive_zone(g.geometry.exclusive_zone);
        layer.wl_surface().commit();
        self.window.set_height(g.geometry.height);
        self.applied = g;
        self.panels.card_top = card_top(cfg.bar_style);
    }

    /// Opens or closes `item`'s panel under the item's zone.
    pub fn toggle_panel(&mut self, ctx: &mut Ctx, item: Item) {
        if let Some(id) = panel_for(item) {
            let zone = self.model.zone(item);
            self.panels.toggle(ctx, id, Some(zone));
        }
    }

    /// The bar's and its panels' surfaces, for dropping input focus when they go.
    pub fn surface_ids(&self) -> Vec<ObjectId> {
        let mut ids = self.panels.surface_ids();
        ids.push(self.window.layer.wl_surface().id());
        ids
    }

    /// Re-applies the geometry from `cfg` alone when its style or autohide differs from what the bar shows.
    pub fn apply_config(&mut self, cfg: &Config) {
        let autohide = autohide_enabled_for(cfg, &self.output);
        let style_changed = cfg.bar_style != self.style;
        self.style = cfg.bar_style;
        if autohide != self.autohide.enabled {
            self.autohide.enabled = autohide;
            self.autohide.hidden = false;
            self.autohide.collapsed = autohide && self.autohide.collapsed;
            self.autohide.opacity.set(if self.autohide.collapsed { 0.0 } else { 1.0 });
            self.apply_geometry(cfg);
        } else if style_changed {
            self.apply_geometry(cfg);
        }
    }

    /// Re-applies the geometry when the compositor's hug radius or outer gap changed; true when it did.
    pub fn tick(&mut self, cfg: &Config) -> bool {
        let wanted = geometry_for(cfg, &self.output, self.autohide.collapsed, &self.controls.compositor.state());
        if wanted == self.applied {
            return false;
        }
        self.apply_geometry(cfg);
        true
    }

    /// The pointer over this bar's surface, `None` when it left.
    pub fn set_pointer(&mut self, at: Option<(f64, f64)>) {
        self.pointer = at;
    }

    fn model_point(&self, x: f64, y: f64) -> (f64, f64) {
        (x, y - self.content_offset(self.style) as f64)
    }

    /// Acts on a left click; returns what the shell should open.
    pub fn press(&mut self, x: f64, y: f64) -> BarPress {
        let (x, y) = self.model_point(x, y);
        let action = self.model.press(x, y);
        match action.kind {
            ActionKind::Toggle if action.item == Some(Item::Logout) => BarPress::Logout,
            ActionKind::Toggle => action.item.map_or(BarPress::Nothing, BarPress::Panel),
            ActionKind::Overview => BarPress::Overview,
            ActionKind::Workspace => {
                self.controls.compositor.focus_workspace(action.workspace, true);
                BarPress::Nothing
            }
            ActionKind::None => BarPress::Nothing,
        }
    }

    /// Volume or brightness by 1 % under the pointer (volume to 150 %); up raises.
    pub fn wheel(&mut self, dy: f64) {
        let Some((x, y)) = self.pointer else { return };
        let (x, y) = self.model_point(x, y);
        let hit = self.model.press(x, y);
        if hit.kind != ActionKind::Toggle {
            return;
        }
        let up = dy < 0.0;
        match hit.item {
            Some(Item::Volume) => {
                if let Some(audio) = &self.controls.audio
                    && audio.sink_id() != 0
                {
                    let next = (audio.sink().percent + if up { VOLUME_STEP } else { -VOLUME_STEP }).clamp(0, VOLUME_MAX);
                    audio.set_volume(audio.sink_id(), next);
                }
            }
            Some(Item::Brightness) => {
                if let Some(b) = &self.controls.brightness
                    && b.available_on(&self.output)
                {
                    let next = (b.percent_on(&self.output) + if up { BRIGHTNESS_STEP } else { -BRIGHTNESS_STEP }).clamp(0, 100);
                    b.set_on(&self.output, next);
                }
            }
            _ => {}
        }
    }

    pub fn wants_hand(&self) -> bool {
        self.pointer.is_some_and(|(x, y)| {
            let (x, y) = self.model_point(x, y);
            self.model.clickable(x, y)
        })
    }

    /// Draws one frame; true when another frame is wanted (a tween is running).
    pub fn draw(&mut self, egl: &Egl, renderer: &mut Renderer, cfg: &Config, open: Option<Item>, keep_shown: bool) -> bool {
        let now = self.window.frame_time();
        self.animations.tick(now);
        if self.autohide.faded.take() && self.autohide.hidden && !self.autohide.collapsed {
            self.autohide.collapsed = true;
            self.apply_geometry(cfg);
        }
        let style = style_spec(cfg.bar_style, true);
        self.model.set_style(&style);
        self.model.set_expand_all(expand_widgets_for(cfg, &self.output));
        self.model.set_open(open, now);
        if self.autohide.enabled {
            let want_shown = self.pointer.is_some() || open.is_some() || keep_shown;
            if want_shown == self.autohide.hidden {
                self.autohide.hidden = !want_shown;
                if want_shown && self.autohide.collapsed {
                    self.autohide.collapsed = false;
                    self.apply_geometry(cfg);
                }
                let (target, ms) = if want_shown { (1.0, AUTOHIDE_REVEAL_MS) } else { (0.0, AUTOHIDE_HIDE_MS) };
                let faded = self.autohide.faded.clone();
                let from = self.autohide.opacity.get();
                self.animations.animate(from, target, ms, Easing::EaseOutCubic, animation::writer(&self.autohide.opacity), Some(Box::new(move |_| faded.set(true))), AUTOHIDE_OWNER);
            }
        }

        if !self.window.begin(egl, renderer) {
            return false;
        }
        let offset = if self.autohide.enabled { style.top_margin as f32 } else { 0.0 };
        let (width, height) = (self.window.width as f32, bar_layout::HEIGHT as f32);
        let scale = self.window.scale;
        {
            let mut frame = self.canvas.begin(renderer, scale);
            frame.set_opacity(if self.autohide.enabled { self.autohide.opacity.get() } else { 1.0 });
            self.model.refresh(now);
            let pointer = self.pointer.map(|(x, y)| (x, y - offset as f64));
            self.model.hover(pointer.map(|p| p.0), pointer.map(|p| p.1), now);
            self.model.tick(now);
            self.model.layout(&mut frame, width);
            frame.begin_group(Rect { x: 0.0, y: offset, w: width, h: height }, GroupOptions::default());
            let f = self.model.frame();
            frame_base(&mut frame, &style, &f, width, height);
            paint(&mut frame, &self.model);
            let layout = self.model.layout_ref();
            let hug = if cfg.bar_style == BarStyle::Okinami && !self.autohide.enabled { self.controls.compositor.state().hug_radius_px as f32 } else { 0.0 };
            frame_overlay(&mut frame, &style, &f, width, height, hug, layout.left_end.is_some(), layout.right_start.is_some());
            frame.end_group();
            frame.flush();
        }
        self.window.end(egl);
        self.animations.has_active() || self.model.animating()
    }

    /// Panels (popup first) before the bar; textures first (the context made current without a surface), then the surface in the phase 2 order.
    pub fn destroy(self, egl: &Egl, renderer: &Renderer) {
        self.panels.destroy(egl, renderer);
        if egl.make_current(None) {
            self.canvas.destroy(renderer);
        }
        self.window.destroy(egl);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::settings::{BarStyle, Config};
    use crate::service::compositor::CompositorState;

    #[test]
    fn bar_apply_config_reads_new() {
        let compositor = CompositorState { hug_radius_px: 12, gaps_out_px: Some(14), ..Default::default() };
        let continuous = Config { bar_style: BarStyle::Continuous, autohide: false, ..Default::default() };
        let okinami = Config { bar_style: BarStyle::Okinami, ..continuous.clone() };
        let hidden = Config { autohide: true, ..okinami.clone() };

        let a = geometry_for(&continuous, "DP-1", false, &compositor);
        assert_eq!(a.geometry, Geometry { height: 40, margin_top: 10, exclusive_zone: 40 });
        assert_eq!(a.side_margin, 14, "gaps_out replaces the style margin");
        let b = geometry_for(&okinami, "DP-1", false, &compositor);
        assert_eq!(b.geometry, Geometry { height: 52, margin_top: 0, exclusive_zone: 40 }, "okinami hugs and sits flush");
        assert_eq!(b.side_margin, 0);
        let c = geometry_for(&hidden, "DP-1", false, &compositor);
        assert_eq!(c.geometry, Geometry { height: 40, margin_top: 0, exclusive_zone: 0 }, "autohide reserves nothing and drops the hug");
        assert_eq!(geometry_for(&hidden, "DP-1", true, &compositor).geometry.height, 1, "collapsed autohide is a strip");
        assert_eq!(geometry_for(&continuous, "DP-1", true, &compositor).geometry.height, 40, "collapse means nothing without autohide");
    }
}
