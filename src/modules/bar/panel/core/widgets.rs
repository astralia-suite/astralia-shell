//! Controls the panel contents share: sliders, bars, toggles, buttons, device rows and dialogs.

use super::panel::PanelPaint;
use crate::base::animation::AnimationManager;
use crate::config::panel_config as cfg;
use crate::render::canvas::{Canvas, FontFamily, TextStyle};
use crate::render::geometry::Rect;
use crate::render::icons;
use crate::render::marquee::{MarqueeTextState, draw_marquee_text};
use crate::render::sticker::{StickerStyle, selectable, sticker};
use crate::render::theme::palette;
use crate::render::tokens::{Color, lerp_color, metrics};

const TEXT_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::TEXT_PX, bold: false, max_width: 0, wrap: false };
const ICON_STYLE: TextStyle = TextStyle { family: FontFamily::Icon, px: cfg::ICON_PX, bold: false, max_width: 0, wrap: false };
const CODE_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::PAIRING_CODE_PX, bold: false, max_width: 0, wrap: false };

fn flat(outline: Color, border_width: f32, shadow: f32) -> StickerStyle {
    StickerStyle { outline, border_width, shadow, ..StickerStyle::default() }
}

/// A track `track` high across `r`, filled to `value` (0..1), with a knob at the fill's end.
pub fn slider(canvas: &mut dyn Canvas, r: Rect, value: f32, dimmed: bool, focused: bool, track: f32) {
    let p = palette();
    let track_y = r.y + (r.h - track) / 2.0;
    let radius = track / 2.0;
    let border = metrics::BORDER_THIN;
    sticker(canvas, Rect { y: track_y, h: track, ..r }, radius, p.surface_alt, &flat(p.border_outer, border, 0.0));
    let fill = r.w * value.clamp(0.0, 1.0);
    if fill > border {
        let bar = Rect { x: r.x + border, y: track_y + border, w: fill - border, h: track - 2.0 * border };
        canvas.rounded(bar, (radius - border).max(0.0), if dimmed { p.text_muted } else { p.pop_c }, 0.0, Color::default());
    }
    let knob_x = (fill - cfg::SLIDER_KNOB / 2.0).clamp(0.0, r.w - cfg::SLIDER_KNOB);
    let knob_y = r.y + (r.h - cfg::SLIDER_KNOB) / 2.0;
    let knob = Rect { x: r.x + knob_x, y: knob_y, w: cfg::SLIDER_KNOB, h: cfg::SLIDER_KNOB };
    selectable(canvas, knob, metrics::RADIUS_WIDGET, false, focused, p.pop_b, 1.0);
}

/// The slider value (0..1) under pointer `x`.
pub fn slider_value(r: Rect, x: f64) -> f32 {
    if r.w <= 0.0 {
        return 0.0;
    }
    (((x - r.x as f64) / r.w as f64) as f32).clamp(0.0, 1.0)
}

/// A pill filled to `fraction`, never less than `min_fill` wide.
pub fn flat_bar(canvas: &mut dyn Canvas, r: Rect, fraction: f32, min_fill: f32, track: Color, fill: Color) {
    let radius = r.h / 2.0;
    let border = if r.h >= 8.0 { metrics::BORDER_THIN } else { 0.0 };
    sticker(canvas, r, radius, track, &flat(palette().border_outer, border, 0.0));
    let width = min_fill.max(r.w * fraction.clamp(0.0, 1.0));
    let bar = Rect { x: r.x + border, y: r.y + border, w: (width.min(r.w) - border).max(0.0), h: r.h - 2.0 * border };
    canvas.rounded(bar, (radius - border).max(0.0), fill, 0.0, Color::default());
}

pub fn centered_text(canvas: &mut dyn Canvas, text: &str, style: &TextStyle, r: Rect, color: Color) {
    let size = canvas.measure(text, style);
    canvas.text(text, style, r.x + (r.w - size.w) / 2.0, r.y + (r.h - size.h) / 2.0, color);
}

/// `text` at `x`, centred vertically in `row`.
pub fn text_in_row(canvas: &mut dyn Canvas, text: &str, style: &TextStyle, x: f32, row: Rect, color: Color) {
    let size = canvas.measure(text, style);
    canvas.text(text, style, x, row.y + (row.h - size.h) / 2.0, color);
}

/// `text` ending at `right`, centred vertically in `row`; returns its width.
pub fn right_text(canvas: &mut dyn Canvas, text: &str, style: &TextStyle, right: f32, row: Rect, color: Color) -> f32 {
    let size = canvas.measure(text, style);
    canvas.text(text, style, right - size.w, row.y + (row.h - size.h) / 2.0, color);
    size.w
}

pub fn toggle(canvas: &mut dyn Canvas, r: Rect, on: bool) {
    let p = palette();
    selectable(canvas, r, metrics::RADIUS_WIDGET, on, false, p.surface_alt, 1.0);
    let knob = r.h - 8.0;
    let x = if on { r.x + r.w - 4.0 - knob } else { r.x + 4.0 };
    canvas.rounded(Rect { x, y: r.y + 4.0, w: knob, h: knob }, metrics::RADIUS_WIDGET.min(knob / 2.0), p.text, 0.0, Color::default());
}

pub fn icon_button(canvas: &mut dyn Canvas, r: Rect, glyph: &str, color: Color) {
    selectable(canvas, r, metrics::RADIUS_WIDGET, false, false, palette().accent_container, 1.0);
    centered_text(canvas, glyph, &ICON_STYLE, r, color);
}

/// One network or Bluetooth device: glyph, title over subtitle, and connect/forget buttons.
pub struct DeviceRow<'a> {
    pub glyph: &'a str,
    pub title: &'a str,
    pub subtitle: &'a str,
    pub background: Color,
    pub glyph_color: Color,
    pub title_color: Color,
    pub subtitle_color: Color,
    pub connected: bool,
    pub busy: bool,
    pub can_forget: bool,
    /// Scrolls a title that does not fit instead of ellipsizing it.
    pub marquee: Option<&'a mut MarqueeTextState>,
    pub animations: Option<&'a mut AnimationManager>,
}

pub fn device_row(canvas: &mut dyn Canvas, r: Rect, row: &mut DeviceRow, paint: &mut PanelPaint, connect_id: i32, forget_id: i32, index: i32) {
    let p = palette();
    let mut face = lerp_color(p.paper, row.background, row.background.a);
    face.a = 1.0;
    sticker(canvas, r, metrics::RADIUS_WIDGET, face, &flat(p.border_outer, metrics::BORDER_THIN, metrics::SHADOW_WIDGET));
    let glyph = canvas.measure(row.glyph, &ICON_STYLE);
    canvas.text(row.glyph, &ICON_STYLE, r.x + cfg::ROW_ICON_GAP, r.y + (r.h - glyph.h) / 2.0, row.glyph_color);

    let right = r.x + r.w - cfg::ROW_ICON_GAP;
    let mut actions;
    if row.busy {
        let busy = "Connecting\u{2026}";
        let size = canvas.measure(busy, &TEXT_STYLE);
        actions = size.w;
        canvas.text(busy, &TEXT_STYLE, right - size.w, r.y + (r.h - size.h) / 2.0, p.lavender);
    } else {
        let size = cfg::CLOSE_BUTTON;
        let y = r.y + (r.h - size) / 2.0;
        let connect = Rect { x: right - size, y, w: size, h: size };
        let (glyph, color) = if row.connected { (icons::NETWORK_DISCONNECT, p.accent) } else { (icons::NETWORK_CONNECT, p.text) };
        icon_button(canvas, connect, glyph, color);
        paint.push(super::panel::PanelRegion { rect: connect, id: connect_id, a: index, ..Default::default() });
        actions = size;
        if row.can_forget {
            let forget = Rect { x: connect.x - cfg::ACTION_GAP - size, ..connect };
            icon_button(canvas, forget, icons::CLOSE, p.text_muted);
            paint.push(super::panel::PanelRegion { rect: forget, id: forget_id, a: index, ..Default::default() });
            actions += cfg::ACTION_GAP + size;
        }
    }

    let text_x = r.x + cfg::ROW_TEXT_LEFT;
    let text_w = (right - actions - cfg::ACTION_GAP - text_x).max(0.0);
    let bounded = TextStyle { max_width: text_w as i32, ..TEXT_STYLE };
    let title = canvas.measure(row.title, &bounded);
    let title_h = if row.marquee.is_some() { canvas.measure(row.title, &TEXT_STYLE).h } else { title.h };
    let title_y = if row.subtitle.is_empty() { r.y + r.h / 2.0 - title_h / 2.0 } else { r.y + r.h / 2.0 - title_h - 1.0 };
    match (row.marquee.as_deref_mut(), row.animations.as_deref_mut()) {
        (Some(marquee), Some(am)) => draw_marquee_text(canvas, am, marquee, row.title, TEXT_STYLE, text_x, title_y, text_w, row.title_color),
        _ => canvas.text(row.title, &bounded, text_x, title_y, row.title_color),
    }
    if !row.subtitle.is_empty() {
        canvas.text(row.subtitle, &bounded, text_x, r.y + r.h / 2.0 + 1.0, row.subtitle_color);
    }
}

/// The dialog card with its (shortened) label and a close button.
pub fn dialog_top(canvas: &mut dyn Canvas, r: Rect, label: &str, paint: &mut PanelPaint, close_id: i32) {
    let p = palette();
    sticker(canvas, r, metrics::RADIUS_CARD, p.paper, &StickerStyle::default());
    let chars = cfg::DIALOG_LABEL_CHARS as usize;
    let shown = if label.chars().count() <= chars { label.to_string() } else { label.chars().take(chars - 1).chain(['\u{2026}']).collect() };
    canvas.text(&shown, &TEXT_STYLE, r.x + cfg::PADDING, r.y + cfg::DIALOG_TOP_MARGIN, p.text_dim);
    let close = Rect { x: r.x + r.w - cfg::DIALOG_TOP_MARGIN - cfg::DIALOG_BUTTON, y: r.y + cfg::DIALOG_TOP_MARGIN, w: cfg::DIALOG_BUTTON, h: cfg::DIALOG_BUTTON };
    icon_button(canvas, close, icons::CLOSE, p.text);
    paint.region(close, close_id);
}

/// A button `label` sized to its text, starting at `x` in the button row at `y`.
#[allow(clippy::too_many_arguments)]
fn button(canvas: &mut dyn Canvas, label: &str, x: f32, y: f32, w: f32, idle: Color, paint: &mut PanelPaint, id: i32) {
    let r = Rect { x, y, w, h: cfg::DIALOG_BUTTON };
    selectable(canvas, r, metrics::RADIUS_WIDGET, false, false, idle, 1.0);
    centered_text(canvas, label, &TEXT_STYLE, r, palette().text);
    paint.region(r, id);
}

fn button_width(canvas: &mut dyn Canvas, label: &str) -> f32 {
    canvas.measure(label, &TEXT_STYLE).w + cfg::DIALOG_BUTTON_PAD
}

#[allow(clippy::too_many_arguments)]
pub fn confirm_dialog(canvas: &mut dyn Canvas, r: Rect, label: &str, prompt: &str, confirm_label: &str, paint: &mut PanelPaint, cancel_id: i32, confirm_id: i32) {
    let p = palette();
    dialog_top(canvas, r, label, paint, cancel_id);
    let x = r.x + cfg::PADDING;
    let width = r.w - 2.0 * cfg::PADDING;
    let mut y = r.y + cfg::PADDING + cfg::DIALOG_SPACER;
    centered_text(canvas, prompt, &TEXT_STYLE, Rect { x, y, w: width, h: cfg::DIALOG_LABEL }, p.text);
    y += cfg::DIALOG_LABEL + cfg::ROW_GAP;
    let ok_w = button_width(canvas, confirm_label);
    let cancel_w = button_width(canvas, "Cancel");
    let bx = x + (width - (ok_w + cfg::ROW_GAP + cancel_w)) / 2.0;
    button(canvas, confirm_label, bx, y, ok_w, p.critical_container, paint, confirm_id);
    button(canvas, "Cancel", bx + ok_w + cfg::ROW_GAP, y, cancel_w, p.accent_container, paint, cancel_id);
}

/// The pairing code with a hint, a Cancel button and, when `yes_id` is non-zero, a Yes button.
#[allow(clippy::too_many_arguments)]
pub fn pairing_dialog(canvas: &mut dyn Canvas, r: Rect, title: &str, code: &str, hint: &str, paint: &mut PanelPaint, cancel_id: i32, yes_id: i32) {
    let p = palette();
    dialog_top(canvas, r, title, paint, cancel_id);
    let x = r.x + cfg::PADDING;
    let width = r.w - 2.0 * cfg::PADDING;
    let mut y = r.y + cfg::PADDING + cfg::DIALOG_SPACER;
    centered_text(canvas, code, &CODE_STYLE, Rect { x, y, w: width, h: cfg::PAIRING_CODE_HEIGHT }, p.accent);
    y += cfg::PAIRING_CODE_HEIGHT + cfg::ROW_GAP;
    centered_text(canvas, hint, &TEXT_STYLE, Rect { x, y, w: width, h: cfg::DIALOG_LABEL }, p.text_dim);
    y += cfg::DIALOG_LABEL + cfg::ROW_GAP;
    let cancel_w = button_width(canvas, "Cancel");
    let yes_w = if yes_id != 0 { button_width(canvas, "Yes") } else { 0.0 };
    let total = cancel_w + if yes_id != 0 { cfg::ROW_GAP + yes_w } else { 0.0 };
    let mut bx = x + (width - total) / 2.0;
    if yes_id != 0 {
        button(canvas, "Yes", bx, y, yes_w, p.accent_container, paint, yes_id);
        bx += yes_w + cfg::ROW_GAP;
    }
    button(canvas, "Cancel", bx, y, cancel_w, p.critical_container, paint, cancel_id);
}
