//! The segmented ring gauge, rasterised with cairo and cached per size, value and colour.

use pangocairo::cairo;

use super::renderer::{Renderer, Texture};
use super::text::{Raster, unpremultiply_bgra};
use super::texture_cache::TextureCache;
use crate::render::tokens::Color;

const SEGMENTS: i32 = 10;
const SEGMENT_GAP_DEG: f32 = 6.0;

/// Values are bucketed to whole percents so a moving value reuses textures.
pub fn cached_arc_gauge<'c>(cache: &'c mut TextureCache, renderer: &Renderer, scale: i32, diameter: f32, stroke: f32, value01: f32, color: Color) -> Option<&'c Texture> {
    let px_diameter = (diameter * scale as f32) as i32;
    let bucket = (value01.clamp(0.0, 1.0) * 100.0).round() as i32;
    let channel = |c: f32| (c * 255.0) as i32;
    let key = format!(
        "arc_gauge:{px_diameter}:{}:{bucket}:{},{},{}",
        (stroke * scale as f32) as i32,
        channel(color.r),
        channel(color.g),
        channel(color.b)
    );
    cache.get(renderer, &key, || rasterize(px_diameter, stroke * scale as f32, bucket, color, scale).unwrap_or_default())
}

fn rasterize(px_diameter: i32, px_stroke: f32, bucket: i32, color: Color, scale: i32) -> Option<Raster> {
    let mut surface = cairo::ImageSurface::create(cairo::Format::ARgb32, px_diameter, px_diameter).ok()?;
    {
        let cr = cairo::Context::new(&surface).ok()?;
        let c = px_diameter as f32 / 2.0;
        let radius = c - px_stroke / 2.0;
        let start = -std::f32::consts::FRAC_PI_2;
        let step = std::f32::consts::TAU / SEGMENTS as f32;
        let gap = SEGMENT_GAP_DEG.to_radians();
        let span = step - gap;
        cr.set_line_width(px_stroke as f64);
        cr.set_line_cap(cairo::LineCap::Butt);
        let lit = bucket as f32 / 100.0 * SEGMENTS as f32;
        let (r, g, b) = (color.r as f64, color.g as f64, color.b as f64);
        for i in 0..SEGMENTS {
            let seg_start = start + i as f32 * step + gap / 2.0;
            cr.set_source_rgba(r, g, b, 0.15);
            cr.arc(c as f64, c as f64, radius as f64, seg_start as f64, (seg_start + span) as f64);
            cr.stroke().ok()?;
            let fill = (lit - i as f32).clamp(0.0, 1.0);
            if fill > 0.0 {
                cr.set_source_rgba(r, g, b, color.a as f64);
                cr.arc(c as f64, c as f64, radius as f64, seg_start as f64, (seg_start + span * fill) as f64);
                cr.stroke().ok()?;
            }
        }
    }
    surface.flush();
    let stride = surface.stride();
    let data = surface.data().ok()?;
    Some(Raster { width: px_diameter, height: px_diameter, scale, rgba: unpremultiply_bgra(&data, px_diameter, px_diameter, stride) })
}
