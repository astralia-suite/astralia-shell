//! Paints the OSD card.

use super::model::Model;
use crate::config::osd_config as cfg;
use crate::render::canvas::{Canvas, FontFamily, TextStyle};
use crate::render::geometry::Rect;
use crate::render::sticker::{StickerStyle, sticker};
use crate::render::theme::palette;
use crate::render::tokens::{lerp_color, metrics};

const WIDTH: f32 = cfg::WIDTH as f32;
const HEIGHT: f32 = cfg::HEIGHT as f32;

/// The card at the surface origin, faded by the model's opacity.
pub fn paint(canvas: &mut dyn Canvas, model: &Model) {
    canvas.set_opacity(model.opacity());
    if model.opacity() <= 0.0 {
        return;
    }
    let p = palette();
    sticker(canvas, Rect { x: 0.0, y: 0.0, w: WIDTH, h: HEIGHT }, metrics::RADIUS_CARD, p.paper, &StickerStyle::default());

    let content_margin = cfg::CONTENT_MARGIN as f32;
    let bar_margin = cfg::BAR_MARGIN as f32;
    let label_width = cfg::LABEL_WIDTH as f32;
    let icon_style = TextStyle { family: FontFamily::Icon, px: cfg::ICON_PX, ..TextStyle::default() };
    let icon_size = canvas.measure(model.glyph(), &icon_style);
    let has_icon = icon_size.w > 0.0;
    if has_icon {
        canvas.text(model.glyph(), &icon_style, content_margin, (HEIGHT - icon_size.h) / 2.0, lerp_color(p.text, p.text_muted, model.icon_mix()));
    }

    let track_x = content_margin + if has_icon { cfg::ICON_PX as f32 + bar_margin } else { 0.0 };
    let track_w = WIDTH - track_x - bar_margin - label_width - content_margin;
    let track_y = (HEIGHT - cfg::TRACK_HEIGHT) / 2.0;
    let radius = cfg::TRACK_HEIGHT / 2.0;
    let border = metrics::BORDER_THIN;
    let outline = StickerStyle { outline: p.border_outer, border_width: border, shadow: 0.0, ..StickerStyle::default() };
    sticker(canvas, Rect { x: track_x, y: track_y, w: track_w, h: cfg::TRACK_HEIGHT }, radius, p.surface_alt, &outline);
    let fill_w = track_w * model.bar_fill().clamp(0.0, 1.0);
    if fill_w > border {
        let fill = Rect { x: track_x + border, y: track_y + border, w: fill_w - border, h: cfg::TRACK_HEIGHT - 2.0 * border };
        let color = if model.muted() { p.text_muted } else { p.pop_c };
        canvas.rounded(fill, (radius - border).max(0.0), color, 0.0, color);
    }

    let label_style = TextStyle { family: FontFamily::Text, px: cfg::LABEL_PX, bold: false, max_width: cfg::LABEL_WIDTH, wrap: false };
    let label = model.label();
    let label_size = canvas.measure(&label, &label_style);
    canvas.text(&label, &label_style, WIDTH - content_margin - label_size.w, (HEIGHT - label_size.h) / 2.0, p.text);
}
