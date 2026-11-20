//! `Canvas` over the GL renderer: shapes draw at once, text and images go through texture caches.

use std::cell::RefCell;
use std::collections::{HashMap, HashSet, VecDeque};
use std::rc::Rc;

use super::arc_gauge::cached_arc_gauge;
use super::image::upload_pixels;
use super::renderer::{Affine2D, Renderer, Texture};
use super::text::{advance_px, rasterize_text};
use super::texture_cache::TextureCache;
use crate::render::canvas::{Canvas, FontFamily, GroupOptions, ImageId, NO_IMAGE, TextSize, TextStyle};
use crate::render::decode::{Pixels, asset_path, decode_image};
use crate::service::media_decode::decode_cover_cached;
use crate::render::geometry::{Point, Rect};
use crate::render::tokens::{self, Color};
use crate::service::runtime;

const THUMB_CACHE: usize = 160;
const THUMB_IN_FLIGHT: usize = 2;
const WHITE: [f32; 4] = [1.0; 4];

enum Job {
    Image(String),
    Thumb { key: String, px: i32 },
}

struct Decoded {
    job: Job,
    generation: u64,
    pixels: Option<Pixels>,
}

/// Decodes finished off the loop thread, uploaded at the next `begin`.
#[derive(Default)]
struct Inbox {
    done: Vec<Decoded>,
    notify: Option<Rc<dyn Fn()>>,
}

struct Thumb {
    id: ImageId,
    px: i32,
    pending: bool,
}

/// What a surface keeps between frames: caches, images and decodes in flight.
pub struct GlCanvas {
    cache: TextureCache,
    images: Vec<Option<Texture>>,
    free_slots: Vec<ImageId>,
    image_ids: HashMap<String, ImageId>,
    images_pending: HashSet<String>,
    thumbs: HashMap<String, Thumb>,
    thumb_order: VecDeque<String>,
    inbox: Rc<RefCell<Inbox>>,
    in_flight: usize,
    generation: u64,
    scale: i32,
    snapshot: Option<Texture>,
}


impl GlCanvas {
    pub fn new() -> GlCanvas {
        GlCanvas {
            cache: TextureCache::default(),
            images: Vec::new(),
            free_slots: Vec::new(),
            image_ids: HashMap::new(),
            images_pending: HashSet::new(),
            thumbs: HashMap::new(),
            thumb_order: VecDeque::new(),
            inbox: Rc::default(),
            in_flight: 0,
            generation: 0,
            scale: 1,
            snapshot: None,
        }
    }

    /// Called on the loop thread whenever a decode finishes, so the host can repaint.
    pub fn set_on_image_ready(&mut self, notify: Rc<dyn Fn()>) {
        self.inbox.borrow_mut().notify = Some(notify);
    }

    /// Starts a frame on `renderer` (already in `begin_frame`); finished decodes are uploaded first.
    pub fn begin<'a>(&'a mut self, renderer: &'a mut Renderer, scale: i32) -> GlFrame<'a> {
        if scale != self.scale {
            self.cache.clear(renderer);
            self.scale = scale;
        }
        self.receive(renderer);
        renderer.set_opacity(1.0);
        GlFrame { canvas: self, renderer, groups: Vec::new(), erase: false }
    }

    fn store(&mut self, tex: Texture) -> ImageId {
        match self.free_slots.pop() {
            Some(id) => {
                self.images[id as usize] = Some(tex);
                id
            }
            None => {
                self.images.push(Some(tex));
                (self.images.len() - 1) as ImageId
            }
        }
    }

    fn free(&mut self, renderer: &Renderer, id: ImageId) {
        if let Some(tex) = self.images.get_mut(id as usize).and_then(Option::take) {
            renderer.delete_texture(tex);
            self.free_slots.push(id);
        }
    }

    fn receive(&mut self, renderer: &Renderer) {
        let done = std::mem::take(&mut self.inbox.borrow_mut().done);
        for decoded in done {
            let tex = decoded.pixels.and_then(|p| upload_pixels(renderer, p));
            match decoded.job {
                Job::Image(name) => {
                    self.images_pending.remove(&name);
                    let id = match tex {
                        Some(tex) => self.store(tex),
                        None => {
                            ::log::warn!("canvas: cannot load image {name}");
                            NO_IMAGE
                        }
                    };
                    self.image_ids.insert(name, id);
                }
                Job::Thumb { key, px } => {
                    if decoded.generation != self.generation {
                        if let Some(tex) = tex {
                            renderer.delete_texture(tex);
                        }
                        continue;
                    }
                    self.in_flight = self.in_flight.saturating_sub(1);
                    let Some(thumb) = self.thumbs.get_mut(&key) else {
                        if let Some(tex) = tex {
                            renderer.delete_texture(tex);
                        }
                        continue;
                    };
                    thumb.pending = false;
                    thumb.px = px;
                    let Some(tex) = tex else { continue };
                    let old = thumb.id;
                    if old != NO_IMAGE {
                        self.free(renderer, old);
                    }
                    let id = self.store(tex);
                    if let Some(thumb) = self.thumbs.get_mut(&key) {
                        thumb.id = id;
                    }
                    self.evict_thumbnails(renderer);
                }
            }
        }
    }

    fn evict_thumbnails(&mut self, renderer: &Renderer) {
        while self.thumb_order.len() > THUMB_CACHE {
            let Some(key) = self.thumb_order.pop_back() else { break };
            if let Some(thumb) = self.thumbs.remove(&key)
                && thumb.id != NO_IMAGE
            {
                self.free(renderer, thumb.id);
            }
        }
    }

    fn spawn_decode(&self, job: Job, decode: impl FnOnce() -> Option<Pixels> + Send + 'static) {
        let inbox = Rc::downgrade(&self.inbox);
        let generation = self.generation;
        runtime::spawn(async move {
            let pixels = blocking::unblock(decode).await;
            let Some(inbox) = inbox.upgrade() else { return };
            let notify = {
                let mut inbox = inbox.borrow_mut();
                inbox.done.push(Decoded { job, generation, pixels });
                inbox.notify.clone()
            };
            if let Some(notify) = notify {
                notify();
            }
        });
    }

    fn image(&mut self, name: &str, target_px: i32) -> ImageId {
        if let Some(&id) = self.image_ids.get(name) {
            return id;
        }
        if self.images_pending.insert(name.to_string()) {
            let path = asset_path(name);
            self.spawn_decode(Job::Image(name.to_string()), move || decode_image(&path, target_px).ok());
        }
        NO_IMAGE
    }

    fn start_thumbnail(&mut self, key: &str, px: i32) {
        self.thumbs.entry(key.to_string()).or_insert(Thumb { id: NO_IMAGE, px: 0, pending: false }).pending = true;
        self.in_flight += 1;
        let path = key.to_string();
        self.spawn_decode(Job::Thumb { key: key.to_string(), px }, move || decode_cover_cached(&path, px, px).ok());
    }

    fn thumbnail(&mut self, key: &str, px: i32) -> ImageId {
        if let Some(thumb) = self.thumbs.get(key) {
            let (id, stale) = (thumb.id, px > thumb.px && !thumb.pending);
            if id != NO_IMAGE {
                if let Some(pos) = self.thumb_order.iter().position(|k| k == key)
                    && pos > 0
                    && let Some(k) = self.thumb_order.remove(pos)
                {
                    self.thumb_order.push_front(k);
                }
                if stale && self.in_flight < THUMB_IN_FLIGHT {
                    self.start_thumbnail(key, px);
                }
            }
            return id;
        }
        if self.in_flight >= THUMB_IN_FLIGHT {
            return NO_IMAGE;
        }
        self.thumb_order.push_front(key.to_string());
        self.start_thumbnail(key, px);
        NO_IMAGE
    }

    /// Drops every thumbnail; decodes still in flight are discarded when they land.
    pub fn release_thumbnails(&mut self, renderer: &Renderer) {
        if self.thumbs.is_empty() && self.in_flight == 0 {
            return;
        }
        let ids: Vec<ImageId> = self.thumbs.values().map(|t| t.id).filter(|&id| id != NO_IMAGE).collect();
        for id in ids {
            self.free(renderer, id);
        }
        self.thumbs.clear();
        self.thumb_order.clear();
        self.in_flight = 0;
        self.generation += 1;
    }

    /// Frees the snapshot texture; the context must be current.
    pub fn release_snapshot(&mut self, renderer: &Renderer) {
        if let Some(tex) = self.snapshot.take() {
            renderer.delete_texture(tex);
        }
    }

    /// Deletes every texture; the context must be current.
    pub fn destroy(mut self, renderer: &Renderer) {
        self.release_snapshot(renderer);
        self.cache.clear(renderer);
        for tex in self.images.drain(..).flatten() {
            renderer.delete_texture(tex);
        }
        self.inbox.borrow_mut().notify = None;
    }
}

#[derive(Clone, Copy)]
struct Group {
    x: f32,
    y: f32,
    clip: bool,
    transformed: bool,
}

/// One frame of drawing; closes any open group and resets the opacity when dropped.
pub struct GlFrame<'a> {
    canvas: &'a mut GlCanvas,
    renderer: &'a mut Renderer,
    groups: Vec<Group>,
    erase: bool,
}

fn glyph_key(text: &str, style: &TextStyle) -> String {
    let kind = match style.family {
        FontFamily::Icon => "i",
        FontFamily::Glyph => "g",
        FontFamily::Text if style.bold => "b",
        FontFamily::Text => "n",
    };
    format!("cv:{kind}{}:{}{}{text}", style.px, style.max_width, if style.wrap { "w:" } else { ":" })
}

fn logical_size(tex: &Texture) -> TextSize {
    let s = tex.scale.max(1) as f32;
    TextSize { w: tex.width as f32 / s, h: tex.height as f32 / s }
}

/// With erase on, the shape first clears what is under it, then paints.
fn shape(renderer: &Renderer, erase: bool, draw: impl Fn(&Renderer, bool)) {
    if erase {
        renderer.set_erase_blend(true);
        draw(renderer, true);
        renderer.set_erase_blend(false);
    }
    draw(renderer, false);
}

fn draw_texture(renderer: &Renderer, erase: bool, tex: &Texture, r: Rect, tint: [f32; 4], radius: f32) {
    shape(renderer, erase, |rd, erasing| {
        let tint = if erasing { WHITE } else { tint };
        if radius > 0.0 {
            rd.draw_texture_rect_rounded(r.x, r.y, r.w, r.h, radius, tex, tint);
        } else {
            rd.draw_texture_rect(r.x, r.y, r.w, r.h, tex, tint);
        }
    });
}

impl GlFrame<'_> {
    fn origin(&self) -> (f32, f32) {
        self.groups.last().map_or((0.0, 0.0), |g| (g.x, g.y))
    }

    pub fn flush(self) {}

    /// The renderer under this frame, for shader passes drawn between canvas calls.
    pub fn renderer(&mut self) -> &mut Renderer {
        self.renderer
    }

    fn offset_xy(&self, points: &[Point]) -> Vec<f32> {
        let (ox, oy) = self.origin();
        points.iter().flat_map(|p| [ox + p.x, oy + p.y]).collect()
    }

    /// Draws a texture owned elsewhere (a window capture) into `r`.
    pub fn texture(&mut self, tex: &Texture, r: Rect, radius: f32) {
        let (ox, oy) = self.origin();
        draw_texture(self.renderer, self.erase, tex, Rect { x: ox + r.x, y: oy + r.y, ..r }, WHITE, radius);
    }
}

impl Drop for GlFrame<'_> {
    fn drop(&mut self) {
        while !self.groups.is_empty() {
            self.end_group();
        }
        self.renderer.set_opacity(1.0);
    }
}

impl Canvas for GlFrame<'_> {
    fn rect(&mut self, r: Rect, fill: Color) {
        let (ox, oy) = self.origin();
        shape(self.renderer, self.erase, |rd, erasing| rd.draw_rect(ox + r.x, oy + r.y, r.w, r.h, if erasing { WHITE } else { fill.rgba() }));
    }

    fn rounded(&mut self, r: Rect, radius: f32, fill: Color, border_width: f32, border: Color) {
        let (ox, oy) = self.origin();
        shape(self.renderer, self.erase, |rd, erasing| {
            let (f, b) = if erasing { (WHITE, WHITE) } else { (fill.rgba(), border.rgba()) };
            rd.draw_rounded_rect(ox + r.x, oy + r.y, r.w, r.h, radius, border_width, f, b);
        });
    }

    fn fillet(&mut self, r: Rect, circle_on_right: bool, color: Color) {
        let (ox, oy) = self.origin();
        shape(self.renderer, self.erase, |rd, erasing| rd.draw_fillet(ox + r.x, oy + r.y, r.w, circle_on_right, if erasing { WHITE } else { color.rgba() }));
    }

    fn set_erase(&mut self, on: bool) {
        self.erase = on;
    }

    fn polygon(&mut self, points: &[Point], color: Color) {
        let xy = self.offset_xy(points);
        shape(self.renderer, self.erase, |rd, erasing| rd.draw_polygon(&xy, if erasing { WHITE } else { color.rgba() }));
    }

    fn cut_polygon(&mut self, points: &[Point]) {
        let xy = self.offset_xy(points);
        self.renderer.set_erase_blend(true);
        self.renderer.draw_polygon(&xy, WHITE);
        self.renderer.set_erase_blend(false);
    }

    fn snapshot(&mut self) {
        self.renderer.grab_framebuffer(&mut self.canvas.snapshot);
    }

    fn snapshot_polygon(&mut self, from: &[Point], to: &[Point], alpha: f32) {
        let Some(tex) = self.canvas.snapshot.as_ref() else {
            self.cut_polygon(to);
            return;
        };
        let s = tex.scale.max(1) as f32;
        let (width, height) = (tex.width as f32 / s, tex.height as f32 / s);
        let (ox, oy) = self.origin();
        let xy_uv: Vec<f32> = from.iter().zip(to).flat_map(|(src, dst)| [ox + dst.x, oy + dst.y, src.x / width, 1.0 - src.y / height]).collect();
        self.renderer.draw_textured_polygon(&xy_uv, tex, [1.0, 1.0, 1.0, alpha]);
    }

    fn measure(&mut self, text: &str, style: &TextStyle) -> TextSize {
        if text.is_empty() {
            return TextSize::default();
        }
        let scale = self.canvas.scale;
        self.canvas.cache.get(self.renderer, &glyph_key(text, style), || rasterize_text(text, style, scale)).map(logical_size).unwrap_or_default()
    }

    fn advance(&mut self, style: &TextStyle) -> f32 {
        advance_px(style)
    }

    fn text(&mut self, text: &str, style: &TextStyle, x: f32, y: f32, color: Color) {
        if text.is_empty() {
            return;
        }
        let (ox, oy) = self.origin();
        let scale = self.canvas.scale;
        if let Some(tex) = self.canvas.cache.get(self.renderer, &glyph_key(text, style), || rasterize_text(text, style, scale)) {
            let size = logical_size(tex);
            draw_texture(self.renderer, self.erase, tex, Rect { x: ox + x, y: oy + y, w: size.w, h: size.h }, color.rgba(), 0.0);
        }
    }

    fn image(&mut self, asset: &str, target_px: i32) -> ImageId {
        self.canvas.image(asset, target_px)
    }

    fn thumbnail(&mut self, path: &str, px: i32) -> ImageId {
        self.canvas.thumbnail(path, px)
    }

    fn image_size(&mut self, id: ImageId) -> TextSize {
        usize::try_from(id).ok().and_then(|i| self.canvas.images.get(i)).and_then(Option::as_ref).map(logical_size).unwrap_or_default()
    }

    fn draw_image(&mut self, id: ImageId, r: Rect, tint: Color) {
        let (ox, oy) = self.origin();
        if let Some(tex) = usize::try_from(id).ok().and_then(|i| self.canvas.images.get(i)).and_then(Option::as_ref) {
            draw_texture(self.renderer, self.erase, tex, Rect { x: ox + r.x, y: oy + r.y, ..r }, tint.rgba(), 0.0);
        }
    }

    fn begin_group(&mut self, r: Rect, options: GroupOptions) {
        let (ox, oy) = self.origin();
        let (x, y) = (ox + r.x, oy + r.y);
        let transformed = options.scale != 1.0 || options.rotation != 0.0;
        if transformed {
            let (cx, cy) = (x + r.w * 0.5, y + r.h * 0.5);
            let model = Affine2D::translation(cx, cy)
                .compose(&Affine2D::scaling(options.scale))
                .compose(&Affine2D::rotation_deg(options.rotation))
                .compose(&Affine2D::translation(-cx, -cy));
            self.renderer.push_model(&model);
        }
        let clip = options.clip && !transformed;
        if clip {
            self.renderer.set_clip(x, y, r.w, r.h);
        }
        self.groups.push(Group { x, y, clip, transformed });
    }

    fn end_group(&mut self) {
        let Some(group) = self.groups.pop() else { return };
        if group.clip {
            self.renderer.clear_clip();
        }
        if group.transformed {
            self.renderer.pop_model();
        }
    }

    fn set_opacity(&mut self, opacity: f32) {
        self.renderer.set_opacity(opacity);
    }

    fn gauge(&mut self, r: Rect, stroke: f32, value: f32, color: Color) {
        let (ox, oy) = self.origin();
        let scale = self.canvas.scale;
        if let Some(tex) = cached_arc_gauge(&mut self.canvas.cache, self.renderer, scale, r.w, stroke, value, color) {
            let size = logical_size(tex);
            draw_texture(self.renderer, self.erase, tex, Rect { x: ox + r.x.round(), y: oy + r.y.round(), w: size.w, h: size.h }, tokens::WHITE.rgba(), 0.0);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::backend::wayland::render::renderer::{offscreen, read_pixel};
    use crate::render::canvas::{GroupOptions, TextStyle};
    use calloop::EventLoop;
    use std::cell::Cell;
    use std::time::Duration;

    const W: i32 = 64;
    const H: i32 = 32;
    const RED: Color = Color { r: 1.0, g: 0.0, b: 0.0, a: 1.0 };

    #[test]
    fn snapshot_polygon_draws() {
        let Some((_egl, mut renderer)) = offscreen(W, H) else { return };
        let mut canvas = GlCanvas::new();
        let p = |x: f32, y: f32| crate::render::geometry::Point { x, y };
        renderer.begin_frame(W, H, 1);
        renderer.clear();
        {
            let mut cv = canvas.begin(&mut renderer, 1);
            cv.rect(Rect { x: 0.0, y: 0.0, w: W as f32, h: H as f32 }, RED);
            cv.snapshot();
        }
        renderer.clear();
        {
            let mut cv = canvas.begin(&mut renderer, 1);
            let left = [p(0.0, 0.0), p(32.0, 0.0), p(32.0, 32.0), p(0.0, 32.0)];
            let right = [p(32.0, 0.0), p(64.0, 0.0), p(64.0, 32.0), p(32.0, 32.0)];
            cv.snapshot_polygon(&left, &right, 1.0);
        }
        assert_eq!(read_pixel(&renderer, H, 48, 16), [255, 0, 0, 255], "the right half shows the snapshot");
        assert_eq!(read_pixel(&renderer, H, 16, 16)[3], 0, "the left half stays clear");

        {
            let mut cv = canvas.begin(&mut renderer, 1);
            cv.polygon(&[p(0.0, 0.0), p(16.0, 0.0), p(16.0, 16.0)], RED);
        }
        assert_eq!(read_pixel(&renderer, H, 12, 2), [255, 0, 0, 255], "a polygon fills its triangle");
        assert_eq!(read_pixel(&renderer, H, 2, 12)[3], 0, "and nothing outside it");
        {
            let mut cv = canvas.begin(&mut renderer, 1);
            cv.cut_polygon(&[p(40.0, 0.0), p(64.0, 0.0), p(64.0, 32.0), p(40.0, 32.0)]);
        }
        assert_eq!(read_pixel(&renderer, H, 50, 16)[3], 0, "a cut clears what it covers");
        assert_eq!(read_pixel(&renderer, H, 36, 16), [255, 0, 0, 255], "and nothing else");

        canvas.release_snapshot(&renderer);
        {
            let mut cv = canvas.begin(&mut renderer, 1);
            cv.snapshot_polygon(&[p(0.0, 0.0), p(8.0, 0.0), p(8.0, 8.0)], &[p(32.0, 0.0), p(40.0, 0.0), p(40.0, 8.0)], 1.0);
        }
        assert_eq!(read_pixel(&renderer, H, 38, 2)[3], 0, "without a snapshot the target is cut");
        canvas.destroy(&renderer);
    }

    #[test]
    fn gl_canvas_draws() {
        let Some((_egl, mut renderer)) = offscreen(W, H) else { return };
        let mut event_loop: EventLoop<'static, ()> = EventLoop::try_new().unwrap();
        crate::service::runtime::attach(&event_loop.handle()).unwrap();
        let ready = Rc::new(Cell::new(0));
        let mut canvas = GlCanvas::new();
        let r = ready.clone();
        canvas.set_on_image_ready(Rc::new(move || r.set(r.get() + 1)));
        let dir = tempfile::tempdir().unwrap();
        let svg = dir.path().join("red.svg");
        std::fs::write(&svg, "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'><rect width='10' height='10' fill='red'/></svg>").unwrap();
        let svg = svg.to_str().unwrap();

        renderer.begin_frame(W, H, 1);
        renderer.clear();
        let style = TextStyle { px: 13, ..Default::default() };
        let size;
        {
            let mut cv = canvas.begin(&mut renderer, 1);
            cv.begin_group(Rect { x: 0.0, y: 0.0, w: 16.0, h: 16.0 }, GroupOptions { clip: true, ..Default::default() });
            cv.rect(Rect { x: 0.0, y: 0.0, w: 32.0, h: 32.0 }, RED);
            cv.end_group();
            size = cv.measure("Hi", &style);
            assert!(size.w > 0.0 && size.h > 0.0);
            cv.text("Hi", &style, 20.0, 0.0, tokens::WHITE);
            assert_eq!(cv.image(svg, 24), NO_IMAGE, "decoding runs off the loop thread");
            assert_eq!(cv.image("stiletto.svg", 24), NO_IMAGE);
            cv.flush();
        }
        assert_eq!(read_pixel(&renderer, H, 8, 8), [255, 0, 0, 255]);
        assert_eq!(read_pixel(&renderer, H, 12, 24)[3], 0, "the clipped group hides what lies outside it");
        let inked = (20..20 + size.w as i32).any(|x| (0..size.h as i32).any(|y| read_pixel(&renderer, H, x, y)[3] > 0));
        assert!(inked, "text draws where measure says");
        assert!((0..H).all(|y| read_pixel(&renderer, H, 20 + size.w as i32 + 2, y)[3] == 0), "and nowhere right of it");

        for _ in 0..100 {
            if ready.get() >= 2 {
                break;
            }
            event_loop.dispatch(Duration::from_millis(20), &mut ()).unwrap();
        }
        assert_eq!(ready.get(), 2, "the host is asked to repaint once per decoded image");
        renderer.begin_frame(W, H, 1);
        let mut cv = canvas.begin(&mut renderer, 1);
        let id = cv.image(svg, 24);
        assert_ne!(id, NO_IMAGE);
        assert_eq!(cv.image_size(id), TextSize { w: 24.0, h: 24.0 });
        let asset = cv.image("stiletto.svg", 24);
        assert_ne!(asset, NO_IMAGE, "a relative name resolves in the asset directory");
        let fit = cv.image_size(asset);
        assert!(fit.w.max(fit.h) == 24.0, "fitted inside the box: {fit:?}");
        cv.draw_image(id, Rect { x: 40.0, y: 8.0, w: 24.0, h: 24.0 }, tokens::WHITE);
        cv.flush();
        assert_eq!(read_pixel(&renderer, H, 52, 20), [255, 0, 0, 255]);
        assert_eq!(read_pixel(&renderer, H, 38, 20)[3], 0);
        canvas.destroy(&renderer);
    }
}
