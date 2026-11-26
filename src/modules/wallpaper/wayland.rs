//! The wallpaper surface of one output: its columns on the `Background` layer and the desktop menu.

use std::cell::Cell;
use std::rc::Rc;

use smithay_client_toolkit::compositor::CompositorState;
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
use crate::wayland::render::wallpaper_column::{DecodeStatus, FillMode, WallpaperColumn};
use crate::base::input::Button;
use crate::config::settings::Config;
use crate::modules::wallpaper::model::{DesktopMenuAction, DesktopMenuModel};
use crate::modules::wallpaper::view;
use crate::render::theme::palette;
#[cfg(feature = "video")]
use crate::service::media_decode::{ANIMATE_MAX_DECODE_DIM, animate_decode_size};
use crate::service::wallpaper::{wallpaper_column_count_animated, wallpaper_column_path_animated, wallpaper_fill_mode_animated};

pub const LAYER_NAMESPACE: &str = "astralia-shell-wallpaper";

const SPREAD_FRAG: &str = include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/assets/shaders/wallpaper/spread.frag"));

/// What a click on the desktop menu asks the shell to do.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MenuPick {
    Settings,
    Wallpaper { column: usize },
}

pub struct Wallpaper {
    pub window: LayerWindow,
    output_name: String,
    columns: Vec<WallpaperColumn>,
    menu: DesktopMenuModel,
    canvas: GlCanvas,
    /// Compiled on the first spread; `Some(None)` after a failed build, which is not retried.
    spread: Option<Option<glow::Program>>,
    notify: Rc<dyn Fn()>,
    animated_logged: Cell<bool>,
}

impl Wallpaper {
    /// `notify` asks for a repaint when a decode finishes.
    pub fn new(compositor: &CompositorState, layer_shell: &LayerShell, qh: &QueueHandle<State>, output: &WlOutput, name: &str, notify: Rc<dyn Fn()>) -> Wallpaper {
        let surface = compositor.create_surface(qh);
        let layer = layer_shell.create_layer_surface(qh, surface, Layer::Background, Some(LAYER_NAMESPACE), Some(output));
        layer.set_anchor(Anchor::TOP | Anchor::BOTTOM | Anchor::LEFT | Anchor::RIGHT);
        layer.set_exclusive_zone(-1);
        layer.set_keyboard_interactivity(KeyboardInteractivity::None);
        layer.commit();
        Wallpaper {
            window: LayerWindow::new(layer, 1),
            output_name: name.to_string(),
            columns: Vec::new(),
            menu: DesktopMenuModel::default(),
            canvas: GlCanvas::new(),
            spread: None,
            notify,
            animated_logged: Cell::new(false),
        }
    }

    pub fn owns(&self, surface: &WlSurface) -> bool {
        self.window.owns(surface)
    }

    pub fn surface_id(&self) -> wayland_client::backend::ObjectId {
        self.window.layer.wl_surface().id()
    }

    /// A configure; a new size re-decodes every column for it.
    pub fn configure(&mut self, egl: &Egl, renderer: &Renderer, cfg: &Config, width: i32, height: i32) {
        let changed = (width, height) != (self.window.width, self.window.height);
        self.window.set_height(height);
        self.window.configure(egl, width);
        if changed {
            self.sync_from_config(renderer, cfg);
        }
    }

    pub fn set_scale(&mut self, renderer: &Renderer, cfg: &Config, scale: i32) {
        if scale == self.window.scale {
            return;
        }
        self.window.set_scale(scale);
        self.sync_from_config(renderer, cfg);
    }

    /// Matches the columns to the config: count, paths and fill modes. With animated wallpapers on,
    /// the animated slot's columns stream; without the video feature they show the still slot.
    pub fn sync_from_config(&mut self, renderer: &Renderer, cfg: &Config) {
        if self.window.width <= 0 || self.window.height <= 0 {
            return;
        }
        let animated = cfg!(feature = "video") && cfg.wallpaper_animated_enabled;
        if cfg.wallpaper_animated_enabled && !animated && !self.animated_logged.replace(true) {
            ::log::info!("wallpaper: animated column on {} needs the video feature", self.output_name);
        }
        let monitor = self.output_name.as_str();
        #[cfg(feature = "video")]
        if !animated && self.columns.iter().any(WallpaperColumn::is_animated) {
            for column in &mut self.columns {
                column.clear(renderer);
            }
        }
        let count = wallpaper_column_count_animated(cfg, monitor, animated).max(0) as usize;
        for mut column in self.columns.drain(count.min(self.columns.len())..) {
            column.clear(renderer);
        }
        self.columns.resize_with(count, WallpaperColumn::default);
        let scale = self.window.scale.max(1);
        let target_w = self.window.width / count.max(1) as i32 * scale;
        let target_h = self.window.height * scale;
        #[cfg(feature = "video")]
        let (target_w, target_h) = if animated { animate_decode_size(target_w, target_h, ANIMATE_MAX_DECODE_DIM) } else { (target_w, target_h) };
        for (i, column) in self.columns.iter_mut().enumerate() {
            let path = wallpaper_column_path_animated(cfg, monitor, i as i32, animated);
            let mode = if wallpaper_fill_mode_animated(cfg, monitor, i as i32, animated) == "fit" { FillMode::Fit } else { FillMode::Crop };
            if path.is_empty() {
                column.clear(renderer);
                continue;
            }
            #[cfg(feature = "video")]
            if animated {
                if !column.plays(&path, target_w, target_h, mode) {
                    column.set_animated(renderer, &path, target_w, target_h, mode, self.notify.clone());
                }
                continue;
            }
            column.set_static(&path, target_w, target_h, mode, self.notify.clone());
        }
    }

    fn spread_program(&mut self, renderer: &Renderer) -> Option<glow::Program> {
        *self.spread.get_or_insert_with(|| renderer.compile_program(SPREAD_FRAG, "wallpaper_spread").ok())
    }

    /// Holds animated columns on their current frame while the screensaver covers them.
    pub fn pause_animation(&mut self) {
        #[cfg(feature = "video")]
        self.columns.iter().for_each(WallpaperColumn::pause);
    }

    pub fn resume_animation(&mut self) {
        #[cfg(feature = "video")]
        self.columns.iter().for_each(WallpaperColumn::resume);
    }

    /// A column has a decode or a video frame to upload.
    pub fn has_pending(&self) -> bool {
        self.columns.iter().any(WallpaperColumn::has_pending)
    }

    /// Each column's decode status, for the settings warning.
    pub fn statuses(&self) -> Vec<DecodeStatus> {
        self.columns.iter().map(WallpaperColumn::status).collect()
    }

    /// The columns side by side over `width` by `height`, for any surface that shows the wallpaper.
    pub fn draw_columns(&self, renderer: &mut Renderer, width: i32, height: i32) {
        let column_w = width as f32 / self.columns.len().max(1) as f32;
        for (i, column) in self.columns.iter().enumerate() {
            column.draw(renderer, i as f32 * column_w, column_w, height as f32);
        }
    }

    /// Uploads finished decodes and the latest video frames; the context must be current. True when
    /// a column changed.
    pub fn upload(&mut self, renderer: &Renderer) -> bool {
        let program = if self.columns.iter().any(|c| !c.is_empty()) { self.spread_program(renderer) } else { None };
        let mut uploaded = false;
        for column in &mut self.columns {
            uploaded |= column.upload_pending(renderer, program);
            #[cfg(feature = "video")]
            {
                uploaded |= column.upload_video(renderer);
            }
        }
        uploaded
    }

    /// Paints one frame; true while a spread runs, so the caller asks for another.
    pub fn draw(&mut self, egl: &Egl, renderer: &mut Renderer) -> bool {
        let uploaded = self.upload(renderer);
        let program = if self.columns.iter().any(|c| !c.is_empty()) { self.spread_program(renderer) } else { None };
        if !self.window.begin(egl, renderer) {
            return uploaded;
        }
        let base = palette().base;
        let (w, h) = (self.window.width, self.window.height);
        renderer.draw_rect(0.0, 0.0, w as f32, h as f32, base.rgba());
        self.draw_columns(renderer, w, h);
        let column_w = w as f32 / self.columns.len().max(1) as f32;
        let mut animating = false;
        for (i, column) in self.columns.iter_mut().enumerate() {
            animating |= column.draw_transition(renderer, program, i as f32 * column_w, 0.0, column_w, h as f32);
        }
        if self.menu.is_open() {
            let rect = self.menu.rect();
            let mut frame = self.canvas.begin(renderer, self.window.scale);
            view::paint(&mut frame, &self.menu, rect.x, rect.y);
            frame.flush();
        }
        self.window.end(egl);
        animating
    }

    /// A button press at `(x, y)`; returns whether to repaint and what the menu asks for.
    pub fn press(&mut self, button: Button, x: f32, y: f32) -> (bool, Option<MenuPick>) {
        let (w, h) = (self.window.width as f32, self.window.height as f32);
        if w <= 0.0 || h <= 0.0 {
            return (false, None);
        }
        if button == Button::Right {
            let columns = self.columns.len().max(1);
            let column = ((x / (w / columns as f32)) as usize).min(columns - 1);
            if self.menu.toggle(x, y, w, h, column) {
                let rect = self.menu.rect();
                self.menu.hover(x - rect.x, y - rect.y);
            }
            return (true, None);
        }
        if button != Button::Left || !self.menu.is_open() {
            return (false, None);
        }
        let rect = self.menu.rect();
        let entry = self.menu.entry_at(x - rect.x, y - rect.y);
        let column = self.menu.column();
        self.menu.close();
        let pick = entry.map(|i| match DesktopMenuModel::entries()[i].action {
            DesktopMenuAction::Settings => MenuPick::Settings,
            DesktopMenuAction::Wallpaper => MenuPick::Wallpaper { column },
        });
        (true, pick)
    }

    /// Pointer motion over the wallpaper (`Some`) or away from it (`None`); true when the menu hover changed.
    pub fn pointer(&mut self, at: Option<(f32, f32)>) -> bool {
        if !self.menu.is_open() {
            return false;
        }
        match at {
            Some((x, y)) => {
                let rect = self.menu.rect();
                self.menu.hover(x - rect.x, y - rect.y)
            }
            None if self.menu.hovered().is_some() => {
                self.menu.clear_hover();
                true
            }
            None => false,
        }
    }

    pub fn wants_hand(&self) -> bool {
        self.menu.is_open() && self.menu.hovered().is_some()
    }

    /// Textures and the program first (the context made current without a surface), then the surface.
    pub fn destroy(mut self, egl: &Egl, renderer: &Renderer) {
        if egl.make_current(None) {
            for column in &mut self.columns {
                column.clear(renderer);
            }
            self.canvas.destroy(renderer);
            if let Some(Some(program)) = self.spread.take() {
                renderer.delete_program(program);
            }
        }
        self.window.destroy(egl);
    }
}
