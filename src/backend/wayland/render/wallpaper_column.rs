//! One wallpaper column: a still image decoded off the loop thread, uploaded on the next paint, and
//! the spread transition from the previous image when the path changes.

use std::cell::{Cell, RefCell};
use std::rc::Rc;
use std::time::{Instant, SystemTime, UNIX_EPOCH};

use super::image::upload_pixels;
use super::renderer::{Renderer, Texture};
use crate::base::animation;
use crate::base::process::trim_heap;
use crate::render::decode::{Pixels, decode_cover};
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

/// How a column's frames arrive; still columns are always `Idle`. Read by the settings overlay.
pub use crate::service::media_decode::MediaDecodeStatus as DecodeStatus;

/// A frame from the decode worker, waiting for the next paint.
#[cfg(feature = "video")]
pub enum Incoming {
    Rgba(crate::service::media_decode::RgbaFrame),
    Drm(crate::service::media_decode::MediaDrmFrame),
}

/// Where a column's stream leaves its latest frame; the stream holds it weakly.
#[cfg(feature = "video")]
#[derive(Default)]
pub struct VideoInbox {
    generation: Cell<u64>,
    latest: RefCell<Option<Incoming>>,
}

/// Hands `frame` to the column if it still exists and still plays stream `generation`; otherwise the
/// frame is dropped, which releases a pinned surface.
#[cfg(feature = "video")]
pub fn deliver(inbox: &std::rc::Weak<VideoInbox>, generation: u64, frame: Incoming) -> bool {
    let Some(inbox) = inbox.upgrade() else { return false };
    if inbox.generation.get() != generation {
        return false;
    }
    *inbox.latest.borrow_mut() = Some(frame);
    true
}

/// The playing stream of an animated column and what it has put on screen.
#[cfg(feature = "video")]
#[derive(Default)]
struct Video {
    inbox: Rc<VideoInbox>,
    playback: Option<crate::service::media_decode::MediaDecodePlayback>,
    textures: std::collections::HashMap<u64, super::video_texture::VideoTexture>,
    surface: Option<u64>,
    /// The surface on screen and the one before it, held until the next import replaces them.
    pinned: Option<crate::service::media_decode::MediaDrmFrame>,
    pinned_prev: Option<crate::service::media_decode::MediaDrmFrame>,
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
    #[cfg(feature = "video")]
    video: Video,
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
                let pixels = decode_cover(&decode_path, target_w, target_h)?;
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

    /// Drops the image and any decode in flight; a playing stream stops without the loop waiting.
    pub fn clear(&mut self, renderer: &Renderer) {
        #[cfg(feature = "video")]
        self.clear_video(renderer);
        self.generation.set(self.generation.get() + 1);
        self.pending.borrow_mut().take();
        self.path.clear();
        self.end_transition(renderer);
        if let Some(tex) = self.tex.take() {
            renderer.delete_texture(tex);
        }
    }

    /// A decode or a stream frame waits for the next paint.
    pub fn has_pending(&self) -> bool {
        #[cfg(feature = "video")]
        if self.video.inbox.latest.borrow().is_some() {
            return true;
        }
        self.pending.borrow().is_some()
    }

    pub fn is_empty(&self) -> bool {
        #[cfg(feature = "video")]
        if self.video.surface.is_some() {
            return false;
        }
        self.tex.is_none()
    }

    /// Nothing to show, or the picture is up.
    pub fn ready(&self) -> bool {
        self.path.is_empty() || self.tex.is_some()
    }

    /// Draws the image (or the imported video surface) filling or fitting the column at `x`, clipped to it.
    pub fn draw(&self, renderer: &mut Renderer, x: f32, column_w: f32, height: f32) {
        #[cfg(feature = "video")]
        let video = self.video.surface.and_then(|id| self.video.textures.get(&id));
        #[cfg(not(feature = "video"))]
        let video: Option<&Texture> = None;
        let (tw, th) = match (video, &self.tex) {
            (Some(v), _) => (v.width as f32, v.height as f32),
            (None, Some(tex)) => (tex.width as f32, tex.height as f32),
            (None, None) => return,
        };
        let scale = if self.mode == FillMode::Fit { (column_w / tw).min(height / th) } else { (column_w / tw).max(height / th) };
        let (draw_w, draw_h) = (tw * scale, th * scale);
        let (dx, dy) = (x + (column_w - draw_w) / 2.0, (height - draw_h) / 2.0);
        renderer.set_clip(x, 0.0, column_w, height);
        match (video, &self.tex) {
            #[cfg(feature = "video")]
            (Some(v), _) => renderer.draw_video_texture_rect(dx, dy, draw_w, draw_h, v),
            (_, Some(tex)) => renderer.draw_texture_rect(dx, dy, draw_w, draw_h, tex, [1.0; 4]),
            _ => {}
        }
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

    pub fn status(&self) -> DecodeStatus {
        #[cfg(feature = "video")]
        if let Some(playback) = &self.video.playback {
            return playback.status();
        }
        DecodeStatus::Idle
    }
}

#[cfg(feature = "video")]
impl WallpaperColumn {
    pub fn is_animated(&self) -> bool {
        self.video.playback.is_some()
    }

    /// Whether this column already plays exactly `path` at this size and mode.
    pub fn plays(&self, path: &str, target_w: i32, target_h: i32, mode: FillMode) -> bool {
        self.is_animated() && self.path == path && self.mode == mode && self.target_w == target_w && self.target_h == target_h
    }

    /// Streams `path` scaled for a `target_w` by `target_h` column; `notify` asks for a paint per frame.
    pub fn set_animated(&mut self, renderer: &Renderer, path: &str, target_w: i32, target_h: i32, mode: FillMode, notify: Rc<dyn Fn()>) {
        use crate::service::media_decode::{ANIMATE_WALLPAPER_FPS, AnimateFit, animate_scale_filter, media_decode_stream};
        self.clear(renderer);
        (self.path, self.mode, self.target_w, self.target_h) = (path.to_string(), mode, target_w, target_h);
        if target_w <= 0 || target_h <= 0 {
            return;
        }
        let generation = self.video.inbox.generation.get() + 1;
        self.video.inbox.generation.set(generation);
        let zero_copy = renderer.video_caps().is_some();
        ::log::info!("wallpaper: animated column start '{path}' zero_copy_supported={} surface=1", i32::from(zero_copy));
        let filter = animate_scale_filter(target_w, target_h, if mode == FillMode::Fit { AnimateFit::Fit } else { AnimateFit::Crop });
        // Two frames in flight at most; a busy loop drops frames rather than queueing them.
        let (tx, rx) = async_channel::bounded::<Incoming>(2);
        let drm_tx = tx.clone();
        let on_frame = Box::new(move |f| {
            let _ = tx.try_send(Incoming::Rgba(f));
        });
        let on_drm = zero_copy.then(|| {
            Box::new(move |f| {
                let _ = drm_tx.try_send(Incoming::Drm(f));
            }) as Box<dyn FnMut(_) + Send>
        });
        self.video.playback = Some(media_decode_stream(path, &filter, ANIMATE_WALLPAPER_FPS, false, on_frame, on_drm));
        let inbox = Rc::downgrade(&self.video.inbox);
        runtime::spawn(async move {
            while let Ok(frame) = rx.recv().await {
                if deliver(&inbox, generation, frame) {
                    notify();
                } else if inbox.upgrade().is_none_or(|i| i.generation.get() != generation) {
                    return;
                }
            }
        });
    }

    /// Puts the stream's latest frame on screen; true when the column changed.
    pub fn upload_video(&mut self, renderer: &Renderer) -> bool {
        let Some(frame) = self.video.inbox.latest.borrow_mut().take() else { return false };
        match frame {
            Incoming::Rgba(f) => {
                match self.tex.as_ref().filter(|t| t.width == f.width && t.height == f.height) {
                    Some(tex) => renderer.update_rgba(tex, &f.rgba),
                    None => {
                        if let Some(old) = self.tex.take() {
                            renderer.delete_texture(old);
                        }
                        self.tex = renderer.upload_rgba(f.width, f.height, 1, &f.rgba);
                    }
                }
                self.video.surface = None;
                true
            }
            Incoming::Drm(frame) => {
                let Some(caps) = renderer.video_caps() else { return false };
                if let std::collections::hash_map::Entry::Vacant(slot) = self.video.textures.entry(frame.surface_id) {
                    match caps.import(renderer.gl(), &frame) {
                        Some(tex) => {
                            slot.insert(tex);
                        }
                        None => {
                            ::log::info!("wallpaper: video_texture_import failed ({}x{} planes=2), falling back to CPU upload", frame.width, frame.height);
                            if let Some(playback) = &self.video.playback {
                                playback.egl_import_failed();
                            }
                            return false;
                        }
                    }
                }
                self.video.surface = Some(frame.surface_id);
                self.video.pinned_prev = self.video.pinned.replace(frame);
                true
            }
        }
    }

    pub fn pause(&self) {
        if let Some(playback) = &self.video.playback {
            playback.pause();
        }
    }

    pub fn resume(&self) {
        if let Some(playback) = &self.video.playback {
            playback.resume();
        }
    }

    fn clear_video(&mut self, renderer: &Renderer) {
        self.video.inbox.generation.set(self.video.inbox.generation.get() + 1);
        self.video.inbox.latest.borrow_mut().take();
        if let Some(playback) = self.video.playback.take() {
            playback.stop_detached();
        }
        if let Some(caps) = renderer.video_caps() {
            for (_, tex) in self.video.textures.drain() {
                caps.release(renderer.gl(), tex);
            }
        }
        self.video.surface = None;
        self.video.pinned = None;
        self.video.pinned_prev = None;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[cfg(feature = "video")]
    #[test]
    fn column_decode_dropped() {
        use crate::service::media_decode::RgbaFrame;
        let frame = || Incoming::Rgba(RgbaFrame { rgba: vec![0; 4], width: 1, height: 1, stride_px: 0 });
        let inbox = Rc::new(VideoInbox::default());
        inbox.generation.set(3);
        let weak = Rc::downgrade(&inbox);
        assert!(deliver(&weak, 3, frame()), "a live column takes its stream's frame");
        assert!(inbox.latest.borrow().is_some());
        assert!(!deliver(&weak, 2, frame()), "a frame from a replaced stream is dropped");
        drop(inbox);
        assert!(!deliver(&weak, 3, frame()), "a frame after the column dropped is released, not uploaded");
    }

    #[cfg(feature = "video")]
    #[test]
    fn column_pending_only_with_a_frame() {
        use crate::service::media_decode::RgbaFrame;
        let column = WallpaperColumn::default();
        assert!(!column.has_pending(), "an idle column asks for no paint");
        column.video.inbox.generation.set(1);
        assert!(deliver(&Rc::downgrade(&column.video.inbox), 1, Incoming::Rgba(RgbaFrame { rgba: vec![0; 4], width: 1, height: 1, stride_px: 0 })));
        assert!(column.has_pending(), "a delivered frame asks for one");
    }

    #[test]
    fn transition_uv_fill() {
        let crop = transition_uv(FillMode::Crop, 100.0, 100.0, 200, 100);
        assert_eq!(crop, [0.5, 1.0, 0.25, 0.0], "a wide image cropped to a square shows its middle half");
        let fit = transition_uv(FillMode::Fit, 100.0, 100.0, 200, 100);
        assert_eq!(fit, [1.0, 2.0, 0.0, -0.5], "a wide image fitted to a square is letterboxed");
    }

    #[test]
    fn column_draws_clipped() {
        use crate::backend::wayland::render::renderer::{offscreen, read_pixel};
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
