//! The custom lock wallpaper: per-output columns from the `Lock` wallpaper slot, decoded off the loop.

use std::collections::HashMap;
use std::rc::Rc;

use crate::wayland::render::renderer::Renderer;
use crate::wayland::render::wallpaper_column::{FillMode, WallpaperColumn};
use crate::config::settings::Config;
use crate::service::wallpaper::{WallpaperSlot, wallpaper_column_count, wallpaper_column_path, wallpaper_fill_mode};

#[derive(Default)]
struct Output {
    columns: Vec<WallpaperColumn>,
    width: i32,
    height: i32,
    scale: i32,
}

/// Whether an output's columns have their pictures; `None` is an output not drawn yet.
fn outputs_ready(loaded: bool, columns: Option<impl IntoIterator<Item = bool>>) -> bool {
    if !loaded {
        return true;
    }
    columns.is_some_and(|c| c.into_iter().all(|ready| ready))
}

pub struct Wallpaper {
    loaded: bool,
    outputs: HashMap<String, Output>,
    /// Asks the lock surfaces to repaint when a decode finishes.
    notify: Rc<dyn Fn()>,
}

impl Wallpaper {
    pub fn new(notify: Rc<dyn Fn()>) -> Wallpaper {
        Wallpaper { loaded: false, outputs: HashMap::new(), notify }
    }

    pub fn load(&mut self) {
        self.loaded = true;
    }

    pub fn loaded(&self) -> bool {
        self.loaded
    }

    /// True once every column of `output_name` shows its picture, or when nothing is loaded.
    pub fn ready(&self, output_name: &str) -> bool {
        outputs_ready(self.loaded, self.outputs.get(output_name).map(|o| o.columns.iter().map(WallpaperColumn::ready)))
    }

    /// Draws the columns of `output_name` over `w` by `h`, resyncing them when the size, scale or count changed.
    #[allow(clippy::too_many_arguments)]
    pub fn draw(&mut self, cfg: &Config, renderer: &mut Renderer, output_name: &str, w: i32, h: i32, scale: i32) {
        if !self.loaded || w <= 0 || h <= 0 {
            return;
        }
        let notify = self.notify.clone();
        let output = self.outputs.entry(output_name.to_string()).or_default();
        let count = wallpaper_column_count(cfg, output_name, WallpaperSlot::Lock).max(0) as usize;
        if output.width != w || output.height != h || output.scale != scale || output.columns.len() != count {
            sync(cfg, renderer, output_name, output, w, h, scale, count, notify);
        }
        for column in &mut output.columns {
            column.upload_pending(renderer, None);
        }
        let column_w = w as f32 / output.columns.len().max(1) as f32;
        for (i, column) in output.columns.iter().enumerate() {
            column.draw(renderer, i as f32 * column_w, column_w, h as f32);
        }
    }

    /// Frees every texture; the context must be current.
    pub fn release(&mut self, renderer: &Renderer) {
        for output in self.outputs.values_mut() {
            for column in &mut output.columns {
                column.clear(renderer);
            }
        }
        self.outputs.clear();
        self.loaded = false;
    }

    /// Frees one output's textures when it goes away; the context must be current.
    pub fn output_removed(&mut self, renderer: &Renderer, output_name: &str) {
        if let Some(mut output) = self.outputs.remove(output_name) {
            for column in &mut output.columns {
                column.clear(renderer);
            }
        }
    }
}

#[allow(clippy::too_many_arguments)]
fn sync(cfg: &Config, renderer: &Renderer, output_name: &str, output: &mut Output, w: i32, h: i32, scale: i32, count: usize, notify: Rc<dyn Fn()>) {
    for mut column in output.columns.drain(count.min(output.columns.len())..) {
        column.clear(renderer);
    }
    output.columns.resize_with(count, WallpaperColumn::default);
    (output.width, output.height, output.scale) = (w, h, scale);
    let target_w = w / count.max(1) as i32 * scale;
    let target_h = h * scale;
    for (i, column) in output.columns.iter_mut().enumerate() {
        let path = wallpaper_column_path(cfg, output_name, i as i32, WallpaperSlot::Lock);
        if path.is_empty() {
            column.clear(renderer);
            continue;
        }
        let mode = if wallpaper_fill_mode(cfg, output_name, i as i32, WallpaperSlot::Lock) == "fit" { FillMode::Fit } else { FillMode::Crop };
        column.set_static(&path, target_w, target_h, mode, notify.clone());
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lock_wallpaper_ready() {
        assert!(outputs_ready(false, None::<[bool; 0]>), "everything is ready while not loaded");
        assert!(outputs_ready(false, Some([false])), "even a pending column");
        assert!(!outputs_ready(true, None::<[bool; 0]>), "an output with no columns yet is not ready");
        assert!(outputs_ready(true, Some([true, true])), "loaded columns are ready");
        assert!(!outputs_ready(true, Some([true, false])), "one pending column holds the output");

        let mut wallpaper = Wallpaper::new(Rc::new(|| {}));
        assert!(wallpaper.ready("DP-1"), "not loaded");
        wallpaper.load();
        assert!(wallpaper.loaded() && !wallpaper.ready("DP-1"), "an unknown output is not ready");
        wallpaper.outputs.insert("DP-1".into(), Output { columns: vec![WallpaperColumn::default()], ..Output::default() });
        assert!(wallpaper.ready("DP-1"), "a column with an empty path counts as ready");
    }
}
