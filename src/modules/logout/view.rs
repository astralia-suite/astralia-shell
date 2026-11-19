//! Paints the logout ring, the logo and the burst's scrim and flash.

use super::layout::{Point, button_center};
use super::model::{COUNT, Model};
use crate::config::logout_config as cfg;
use crate::render::canvas::{Canvas, FontFamily, GroupOptions, NO_IMAGE, TextStyle};
use crate::render::geometry::Rect;
use crate::render::sticker::{StickerStyle, sticker};
use crate::render::theme::palette;
use crate::render::tokens::{WHITE, lerp_color, metrics, with_alpha};

const GLYPH_STYLE: TextStyle = TextStyle { family: FontFamily::Glyph, px: cfg::GLYPH_PX, bold: false, max_width: 0, wrap: false };

/// The overlay over a `width` by `height` output; `logo_path` empty means the bundled logo.
pub fn paint(canvas: &mut dyn Canvas, model: &Model, width: f32, height: f32, logo_path: &str) {
    if !model.open_state() {
        return;
    }
    let p = palette();
    let center = Point { x: f64::from(width) / 2.0, y: f64::from(height) / 2.0 };
    let (cx, cy) = (center.x as f32, center.y as f32);
    let size = cfg::BUTTON_SIZE as f32;
    let full = Rect { x: 0.0, y: 0.0, w: width, h: height };

    if model.scrim() > 0.0 {
        canvas.rect(full, with_alpha(p.shadow, model.scrim()));
    }

    for i in 0..COUNT {
        let t = model.travel(i);
        let visible = if model.exiting() { model.exit_fade() } else { t.clamp(0.0, 1.0) };
        if visible <= 0.002 {
            continue;
        }
        let end = button_center(i, center);
        let bx = cx + (end.x as f32 - cx) * t;
        let by = cy + (end.y as f32 - cy) * t;
        let highlight = 1.0 + (cfg::HIGHLIGHT_SCALE as f32 - 1.0) * model.highlight_scale(i);
        let scale = if model.exiting() { 1.0 } else { t } * highlight;
        let w = size * scale;
        let fill = with_alpha(lerp_color(p.paper, p.accent_alt, model.highlight_border(i)), visible);
        let style = StickerStyle {
            outline: with_alpha(p.border_outer, visible),
            border_width: metrics::BORDER_THICK,
            shadow: metrics::SHADOW_OFFSET * scale,
            ..StickerStyle::default()
        };
        sticker(canvas, Rect { x: bx - w / 2.0, y: by - w / 2.0, w, h: w }, metrics::RADIUS_CARD * scale, fill, &style);
        let glyph = cfg::ACTIONS[i].glyph;
        let extent = canvas.measure(glyph, &GLYPH_STYLE);
        canvas.text(glyph, &GLYPH_STYLE, bx - extent.w / 2.0, by - extent.h / 2.0, with_alpha(p.text, visible));
    }

    let logo_size = cfg::LOGO_SIZE as f32;
    let group = GroupOptions { scale: model.logo_scale(), ..GroupOptions::default() };
    canvas.begin_group(Rect { x: cx - logo_size / 2.0, y: cy - logo_size / 2.0, w: logo_size, h: logo_size }, group);
    let logo = canvas.image(if logo_path.is_empty() { cfg::LOGO_ASSET } else { logo_path }, cfg::LOGO_SIZE);
    if logo != NO_IMAGE {
        let extent = canvas.image_size(logo);
        if extent.w > 0.0 && extent.h > 0.0 {
            let fit = (logo_size / extent.w).min(logo_size / extent.h);
            let (w, h) = (extent.w * fit, extent.h * fit);
            let alpha = if model.exiting() { model.exit_fade() } else { 1.0 };
            canvas.draw_image(logo, Rect { x: (logo_size - w) / 2.0, y: (logo_size - h) / 2.0, w, h }, with_alpha(WHITE, alpha));
        }
    }
    canvas.end_group();

    if model.flash() > 0.0 {
        canvas.rect(full, with_alpha(p.accent_container, model.flash()));
    }
}
