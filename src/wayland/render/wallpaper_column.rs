//! One wallpaper column: a still image decoded off the loop thread, uploaded on the next paint, and
//! the spread transition from the previous image when the path changes.

use std::cell::{Cell, RefCell};
use std::rc::Rc;
use std::time::{Instant, SystemTime, UNIX_EPOCH};

use super::image::upload_pixels;
use super::renderer::{Renderer, Texture};
use crate::base::animation;
use crate::base::process::trim_heap;
use crate::render::decode::Pixels;
use crate::service::media_decode::decode_cover_cached;
use crate::render::palette_extract::{ember_colour, extract_seeds};
use crate::render::spread_map::{SpreadMap, make_spread_map};
use crate::render::theme::palette;
use crate::render::tokens::Color;
use crate::service::runtime;

pub const WALLPAPER_SPREAD_DURATION_MS: f32 = 1800.0;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum FillMode {
    #[default]
    Crop,
    Fit,
}

struct Decoded {
    pixels: Pixels,
    spread: Option<SpreadMap>,
    ember: Color,
}

struct Transition {
    start: Instant,
    map: Texture,
    seed: f32,
    ember: Color,
}

#[derive(Default)]
pub struct WallpaperColumn {
    tex: Option<Texture>,
    prev: Option<Texture>,
    transition: Option<Transition>,
    generation: Rc<Cell<u64>>,
    pending: Rc<RefCell<Option<Decoded>>>,
    path: String,
    mode: FillMode,
    target_w: i32,
    target_h: i32,
}

/// A seed that differs per call; the spread pattern only needs to vary, not to be unpredictable.
fn random_seed(salt: u64) -> u32 {
    let nanos = SystemTime::now().duration_since(UNIX_EPOCH).map_or(0, |d| d.as_nanos() as u64);
    let mut z = nanos ^ salt.wrapping_mul(0x9E37_79B9_7F4A_7C15);
    z = (z ^ (z >> 33)).wrapping_mul(0xFF51_AFD7_ED55_8CCD);
    (z ^ (z >> 33)) as u32
}

/// The `[scale_x, scale_y, offset_x, offset_y]` that maps column UVs into an image of `tw` by `th`.
fn transition_uv(mode: FillMode, col_w: f32, col_h: f32, tw: i32, th: i32) -> [f32; 4] {
    let (tw, th) = (tw as f32, th as f32);
    let scale = if mode == FillMode::Fit { (col_w / tw).min(col_h / th) } else { (col_w / tw).max(col_h / th) };
    let (draw_w, draw_h) = (tw * scale, th * scale);
    [col_w / draw_w, col_h / draw_h, -((col_w - draw_w) / 2.0) / draw_w, -((col_h - draw_h) / 2.0) / draw_h]
}

impl WallpaperColumn {
    /// Shows `path` scaled for a `target_w` by `target_h` device-pixel column; a column already showing
    /// an image spreads into the new one. Nothing happens when it already shows exactly this.
    pub fn set_static(&mut self, path: &str, target_w: i32, target_h: i32, mode: FillMode, notify: Rc<dyn Fn()>) {
        if self.path == path && self.mode == mode && self.target_w == target_w && self.target_h == target_h && self.tex.is_some() {
            return;
        }
        let spread = self.tex.is_some();
        (self.path, self.mode, self.target_w, self.target_h) = (path.to_string(), mode, target_w, target_h);
        let generation = self.generation.get() + 1;
        self.generation.set(generation);
        let (current, pending) = (self.generation.clone(), self.pending.clone());
        let path = path.to_string();
        let seed = random_seed(generation);
        runtime::spawn(async move {
            let decode_path = path.clone();
            let decoded = blocking::unblock(move || {
                let pixels = decode_cover_cached(&decode_path, target_w, target_h)?;
                let (spread, ember) = if spread {
                    let ember = ember_colour(&extract_seeds(&pixels.data, pixels.width, pixels.height, pixels.premultiplied));
                    (Some(make_spread_map(seed)), ember)
                } else {
                    (None, Color { r: 1.0, g: 1.0, b: 1.0, a: 1.0 })
                };
                Ok::<_, String>(Decoded { pixels, spread, ember })
            })
            .await;
            if current.get() != generation {
                return;
            }
            match decoded {
                Ok(decoded) => {
                    *pending.borrow_mut() = Some(decoded);
                    notify();
                }
                Err(e) => ::log::error!("wallpaper: cannot decode {path}: {e}"),
            }
        });
    }

    /// Uploads a finished decode; true when the column changed. The spread starts here.
    pub fn upload_pending(&mut self, renderer: &Renderer, spread_program: Option<glow::Program>) -> bool {
        let Some(decoded) = self.pending.borrow_mut().take() else { return false };
        let Some(fresh) = upload_pixels(renderer, decoded.pixels) else { return false };
        let old = self.tex.replace(fresh);
        self.end_transition(renderer);
        match (old, decoded.spread, spread_program) {
            (Some(old), Some(map), Some(_)) if !animation::instant() => {
                match renderer.upload_rgba_nearest(map.width, map.height, &map.rgba) {
                    Some(map) => {
                        let seed = random_seed(self.generation.get()) as f32 / u32::MAX as f32 * 100.0;
                        self.transition = Some(Transition { start: Instant::now(), map, seed, ember: decoded.ember });
                        self.prev = Some(old);
                    }
                    None => renderer.delete_texture(old),
                }
            }
            (Some(old), _, _) => renderer.delete_texture(old),
            _ => {}
        }
        trim_heap();
        true
    }

    fn end_transition(&mut self, renderer: &Renderer) {
        if let Some(t) = self.transition.take() {
            renderer.delete_texture(t.map);
        }
        if let Some(prev) = self.prev.take() {
            renderer.delete_texture(prev);
        }
    }

    /// Drops the image and any decode in flight.
    pub fn clear(&mut self, renderer: &Renderer) {
        self.generation.set(self.generation.get() + 1);
        self.pending.borrow_mut().take();
        self.path.clear();
        self.end_transition(renderer);
        if let Some(tex) = self.tex.take() {
            renderer.delete_texture(tex);
        }
    }

    /// A decode waits for the next paint.
    pub fn has_pending(&self) -> bool {
        self.pending.borrow().is_some()
    }

    pub fn is_empty(&self) -> bool {
        self.tex.is_none()
    }

    /// Nothing to show, or the picture is up.
    pub fn ready(&self) -> bool {
        self.path.is_empty() || self.tex.is_some()
    }

    /// Draws the image filling or fitting the column at `x`, clipped to it.
    pub fn draw(&self, renderer: &mut Renderer, x: f32, column_w: f32, height: f32) {
        let Some(tex) = &self.tex else { return };
        let (tw, th) = (tex.width as f32, tex.height as f32);
        let scale = if self.mode == FillMode::Fit { (column_w / tw).min(height / th) } else { (column_w / tw).max(height / th) };
        let (draw_w, draw_h) = (tw * scale, th * scale);
        let (dx, dy) = (x + (column_w - draw_w) / 2.0, (height - draw_h) / 2.0);
        renderer.set_clip(x, 0.0, column_w, height);
        renderer.draw_texture_rect(dx, dy, draw_w, draw_h, tex, [1.0; 4]);
        renderer.clear_clip();
    }

    /// Draws the spread over the column while it runs; false once it has ended.
    pub fn draw_transition(&mut self, renderer: &Renderer, program: Option<glow::Program>, x: f32, y: f32, w: f32, h: f32) -> bool {
        let Some(t) = &self.transition else { return false };
        let progress = t.start.elapsed().as_secs_f32() * 1000.0 / WALLPAPER_SPREAD_DURATION_MS;
        let (Some(program), Some(from), Some(to)) = (program, &self.prev, &self.tex) else {
            self.end_transition(renderer);
            return false;
        };
        if progress >= 1.0 {
            self.end_transition(renderer);
            return false;
        }
        let from_uv = transition_uv(self.mode, w, h, from.width, from.height);
        let to_uv = transition_uv(self.mode, w, h, to.width, to.height);
        let fill = palette().base.rgba();
        renderer.draw_custom(program, x, y, w, h, |gl, p| {
            use glow::HasContext;
            // SAFETY: the context is current; every texture id is live until `end_transition`.
            unsafe {
                let uniform = |name: &str| gl.get_uniform_location(p, name);
                for (unit, (tex, name)) in [(from.id, "u_from"), (to.id, "u_to"), (t.map.id, "u_map")].into_iter().enumerate() {
                    gl.active_texture(glow::TEXTURE0 + unit as u32);
                    gl.bind_texture(glow::TEXTURE_2D, Some(tex));
                    gl.uniform_1_i32(uniform(name).as_ref(), unit as i32);
                }
                gl.uniform_4_f32_slice(uniform("u_from_uv").as_ref(), &from_uv);
                gl.uniform_4_f32_slice(uniform("u_to_uv").as_ref(), &to_uv);
                gl.uniform_4_f32_slice(uniform("u_fill").as_ref(), &fill);
                gl.uniform_1_f32(uniform("u_progress").as_ref(), progress);
                gl.uniform_2_f32(uniform("u_map_size").as_ref(), t.map.width as f32, t.map.height as f32);
                gl.uniform_4_f32_slice(uniform("u_ember").as_ref(), &t.ember.rgba());
                gl.uniform_1_f32(uniform("u_seed").as_ref(), t.seed);
            }
        });
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn transition_uv_fill() {
        let crop = transition_uv(FillMode::Crop, 100.0, 100.0, 200, 100);
        assert_eq!(crop, [0.5, 1.0, 0.25, 0.0], "a wide image cropped to a square shows its middle half");
        let fit = transition_uv(FillMode::Fit, 100.0, 100.0, 200, 100);
        assert_eq!(fit, [1.0, 2.0, 0.0, -0.5], "a wide image fitted to a square is letterboxed");
    }

    #[test]
    fn column_draws_clipped() {
        use crate::wayland::render::renderer::{offscreen, read_pixel};
        let Some((_egl, mut r)) = offscreen(64, 32) else { return };
        let spread = include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/assets/shaders/wallpaper/spread.frag"));
        let program = r.compile_program(spread, "wallpaper_spread").expect("the spread shader builds");
        r.delete_program(program);

        let red: Vec<u8> = [255, 0, 0, 255].repeat(8 * 4);
        let mut column = WallpaperColumn { tex: r.upload_rgba(8, 4, 1, &red), ..WallpaperColumn::default() };
        r.begin_frame(64, 32, 1);
        r.clear();
        column.draw(&mut r, 0.0, 32.0, 32.0);
        assert_eq!(read_pixel(&r, 32, 16, 16), [255, 0, 0, 255], "the cropped image fills its column");
        assert_eq!(read_pixel(&r, 32, 48, 16)[3], 0, "and nothing past it");
        column.clear(&r);
        assert!(column.is_empty());
    }
}
