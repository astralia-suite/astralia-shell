//! Item chips: icon cells, pills and labels.

use super::style::StyleSpec;
use crate::config::bar_layout;
use crate::render::canvas::{Canvas, FontFamily, GroupOptions, TextStyle};
use crate::render::geometry::Rect;
use crate::render::sticker::{StickerStyle, sticker};
use crate::render::theme::palette;
use crate::render::tokens::{Color, metrics, with_alpha};

const ICON_STYLE: TextStyle = TextStyle { family: FontFamily::Icon, px: bar_layout::ICON_PX as i32, bold: false, max_width: 0, wrap: false };
const TEXT_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: bar_layout::TEXT_PX, bold: false, max_width: 0, wrap: false };
const NAME_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: bar_layout::ACTIVE_WINDOW_NAME_PX, bold: false, max_width: 0, wrap: false };
const TITLE_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: bar_layout::ACTIVE_WINDOW_TITLE_PX, bold: false, max_width: 0, wrap: false };

const CLIPPED: GroupOptions = GroupOptions { scale: 1.0, clip: true, radius: 0.0, rotation: 0.0 };

/// One icon and its label inside a shared pill.
pub struct IconCell<'a> {
    pub glyph: &'a str,
    pub icon_w: f32,
    pub label: &'a str,
    pub label_w: f32,
    pub amount: f32,
}

/// Width of an icon followed by `amount` of its label (0 collapsed, 1 expanded).
pub fn icon_cell_extent(icon_w: f32, label_w: f32, amount: f32) -> f32 {
    if label_w > 0.0 { icon_w + (label_w + bar_layout::LABEL_GAP) * amount } else { icon_w }
}

pub fn chip_fill() -> Color {
    palette().accent_container
}

pub fn paint_ringed(canvas: &mut dyn Canvas, style: &StyleSpec, r: Rect, fill: Color) {
    let s = StickerStyle { outline: style.border, border_width: style.border_width, shadow: style.shadow, shadow_color: Some(style.shadow_color), shadow_dx: Some(style.shadow_dx), ring: None };
    sticker(canvas, r, style.radius, fill, &s);
}

/// A pill background, for the styles that draw one per item.
pub fn paint_pill(canvas: &mut dyn Canvas, style: &StyleSpec, r: Rect, fill: Color) {
    if style.has_pill_bg() {
        paint_ringed(canvas, style, r, fill);
    }
}

pub fn paint_icon_label(canvas: &mut dyn Canvas, style: &StyleSpec, r: Rect, glyph: &str, label: &str, label_alpha: f32) {
    let p = palette();
    let pad = r.h * style.pad_ratio;
    let dy = style.text_lift;
    canvas.begin_group(r, CLIPPED);
    let icon = canvas.measure(glyph, &ICON_STYLE);
    canvas.text(glyph, &ICON_STYLE, pad, (r.h - icon.h) / 2.0 - dy, p.text);
    if !label.is_empty() && label_alpha > 0.0 {
        let size = canvas.measure(label, &TEXT_STYLE);
        canvas.text(label, &TEXT_STYLE, pad + icon.w + bar_layout::LABEL_GAP, (r.h - size.h) / 2.0 - dy, with_alpha(p.text, label_alpha));
    }
    canvas.end_group();
}

/// The active window: name over title, squeezed into the pill's inner height.
pub fn paint_two_lines(canvas: &mut dyn Canvas, style: &StyleSpec, r: Rect, glyph: &str, title: &str, detail: &str, amount: f32) {
    let p = palette();
    let pad = r.h * style.pad_ratio;
    let dy = style.text_lift;
    canvas.begin_group(r, CLIPPED);
    let icon = canvas.measure(glyph, &ICON_STYLE);
    canvas.text(glyph, &ICON_STYLE, pad, (r.h - icon.h) / 2.0 - dy, p.text);
    let first_style = if detail.is_empty() { TEXT_STYLE } else { NAME_STYLE };
    let a = canvas.measure(title, &first_style);
    let b = canvas.measure(detail, &TITLE_STYLE);
    let x = pad + icon.w + bar_layout::LABEL_GAP;
    let edge = style.border_width + metrics::RING_WIDTH;
    let inner = r.h - style.shadow - 2.0 * edge;
    let stacked = if detail.is_empty() { a.h } else { a.h + b.h };
    let overlap = (stacked - inner).max(0.0);
    let y = edge + (inner - (stacked - overlap)) / 2.0;
    canvas.text(title, &first_style, x, y, with_alpha(p.text, amount));
    if !detail.is_empty() {
        canvas.text(detail, &TITLE_STYLE, x, y + a.h - overlap, with_alpha(p.text_muted, amount));
    }
    canvas.end_group();
}

pub fn paint_badge(canvas: &mut dyn Canvas, style: &StyleSpec, r: Rect, text: &str, fill: Color) {
    paint_pill(canvas, style, r, fill);
    let size = canvas.measure(text, &TEXT_STYLE);
    canvas.text(text, &TEXT_STYLE, r.x + (r.w - size.w) / 2.0, r.y + (r.h - size.h) / 2.0 - style.text_lift, palette().text);
}

pub fn paint_icon_row(canvas: &mut dyn Canvas, style: &StyleSpec, r: Rect, cells: &[IconCell], gap: f32) {
    let p = palette();
    paint_pill(canvas, style, r, chip_fill());
    let mut x = r.h * style.pad_ratio;
    let dy = style.text_lift;
    canvas.begin_group(r, CLIPPED);
    for cell in cells {
        let icon = canvas.measure(cell.glyph, &ICON_STYLE);
        canvas.text(cell.glyph, &ICON_STYLE, x, (r.h - icon.h) / 2.0 - dy, p.text);
        if cell.label_w > 0.0 && cell.amount > 0.0 {
            let size = canvas.measure(cell.label, &TEXT_STYLE);
            canvas.text(cell.label, &TEXT_STYLE, x + cell.icon_w + bar_layout::LABEL_GAP, (r.h - size.h) / 2.0 - dy, with_alpha(p.text, cell.amount));
        }
        x += icon_cell_extent(cell.icon_w, cell.label_w, cell.amount) + gap;
    }
    canvas.end_group();
}
