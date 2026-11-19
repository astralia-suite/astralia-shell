//! Polkit card: title tag, request message, password field and info line.

use crate::config::polkit_config as cfg;
use crate::modules::polkit::layout::{card_height, visible_dots};
use crate::modules::polkit::model::Model;
use crate::render::canvas::{Canvas, FontFamily, GroupOptions, NO_IMAGE, TextStyle};
use crate::render::geometry::Rect;
use crate::render::marquee::draw_marquee_text;
use crate::render::sticker::{StickerStyle, sticker};
use crate::render::text_field::text_field_utf8_len;
use crate::render::theme::palette;
use crate::render::tokens::{Color, WHITE, metrics};

fn style(px: i32, bold: bool) -> TextStyle {
    TextStyle { family: FontFamily::Text, px, bold, ..TextStyle::default() }
}

#[allow(clippy::too_many_arguments)]
fn centered_text(canvas: &mut dyn Canvas, text: &str, mut style: TextStyle, width: f32, cx: f32, cy: f32, color: Color) {
    style.max_width = width as i32;
    let size = canvas.measure(text, &style);
    canvas.text(text, &style, cx - size.w / 2.0, cy - size.h / 2.0, color);
}

#[allow(clippy::too_many_arguments)]
fn line_text(canvas: &mut dyn Canvas, text: &str, style: &TextStyle, x: f32, y: f32, line_height: f32, color: Color) {
    let size = canvas.measure(text, style);
    canvas.text(text, style, x, y + (line_height - size.h) / 2.0, color);
}

fn dots(canvas: &mut dyn Canvas, model: &Model, x0: f32, width: f32, cy: f32) {
    let dot = cfg::DOT_SIZE as f32;
    let length = text_field_utf8_len(model.password());
    let visible = visible_dots(length, f64::from(width));
    let first = length - visible;
    let x = x0 + (width - visible as f32 * dot) / 2.0;
    let echo = canvas.image(cfg::ECHO_FILE_PATH, cfg::DOT_SIZE);
    let chars = &model.dot_anim().chars;
    for i in 0..visible {
        let scale = chars.get(first + i).map_or(1.0, |c| c.scale.get());
        let size = dot * scale;
        let cell_x = x + i as f32 * dot;
        let r = Rect { x: cell_x + (dot - size) / 2.0, y: cy - size / 2.0, w: size, h: size };
        if echo != NO_IMAGE {
            canvas.draw_image(echo, r, WHITE);
        } else {
            canvas.rounded(r, size / 2.0, palette().text, 0.0, Color::default());
        }
    }
}

pub fn paint(canvas: &mut dyn Canvas, model: &mut Model, surface_width: f32, surface_height: f32) {
    if !model.open() {
        return;
    }
    let p = palette();
    let prompt = model.prompt().clone();
    let card_h = card_height(model.show_info()) as f32;
    let card_w = cfg::CARD_WIDTH as f32;
    let card = Rect { x: (surface_width - card_w) / 2.0, y: (surface_height - card_h) / 2.0, w: card_w, h: card_h };

    canvas.begin_group(card, GroupOptions { scale: model.card_scale(), clip: false, ..GroupOptions::default() });
    sticker(canvas, Rect { x: 0.0, y: 0.0, w: card_w, h: card_h }, metrics::RADIUS_CARD, p.paper, &StickerStyle::default());

    let pad = cfg::CARD_PAD as f32;
    let content_w = card_w - pad * 2.0;
    let content_cx = pad + content_w / 2.0;
    let mut y = pad;

    let mut title = style(15, true);
    title.max_width = content_w as i32;
    let title_size = canvas.measure(cfg::TITLE_TEXT, &title);
    let tag_pad = cfg::TITLE_TAG_PAD as f32;
    let tag_style = StickerStyle { outline: p.border_outer, border_width: metrics::BORDER_THIN, shadow: metrics::SHADOW_WIDGET, ..StickerStyle::default() };
    let tag = Rect { x: pad - tag_pad, y: y - tag_pad / 2.0, w: title_size.w + 2.0 * tag_pad, h: cfg::TITLE_LINE_HEIGHT as f32 + tag_pad + metrics::SHADOW_WIDGET };
    sticker(canvas, tag, metrics::RADIUS_WIDGET, p.accent, &tag_style);
    line_text(canvas, cfg::TITLE_TEXT, &title, pad, y, cfg::TITLE_LINE_HEIGHT as f32, p.text);
    y += (cfg::TITLE_LINE_HEIGHT + cfg::SPACING) as f32;

    let (animations, marquee) = model.marquee();
    draw_marquee_text(canvas, animations, marquee, &prompt.message, style(12, false), pad, y, content_w, p.text_muted);
    y += (cfg::MESSAGE_LINE_HEIGHT + cfg::SPACING) as f32;

    let field_h = cfg::FIELD_HEIGHT as f32;
    let field_style = StickerStyle { outline: p.border_outer, border_width: metrics::BORDER_THICK, shadow: 0.0, ..StickerStyle::default() };
    sticker(canvas, Rect { x: pad, y, w: content_w, h: field_h }, metrics::RADIUS_WIDGET, if prompt.needs_input { p.pop_b } else { p.surface }, &field_style);
    let field_cy = y + field_h / 2.0;
    let dot_margin = cfg::DOT_MARGIN as f32;
    let dots_w = content_w - dot_margin * 2.0;
    if prompt.needs_input {
        if !model.error().is_empty() {
            let error = model.error().to_string();
            centered_text(canvas, &error, style(12, false), dots_w, content_cx, field_cy, p.critical);
        } else if model.password().is_empty() {
            centered_text(canvas, cfg::PASSWORD_PLACEHOLDER, style(12, false), dots_w, content_cx, field_cy, p.text_muted);
        } else {
            dots(canvas, model, pad + dot_margin, dots_w, field_cy);
        }
    } else {
        centered_text(canvas, cfg::AUTHENTICATING_TEXT, style(12, false), dots_w, content_cx, field_cy, p.text_muted);
    }
    y += field_h;

    if model.show_info() {
        y += cfg::SPACING as f32;
        let mut info = style(11, false);
        info.max_width = content_w as i32;
        line_text(canvas, &prompt.info, &info, pad, y, cfg::INFO_LINE_HEIGHT as f32, p.text_muted);
    }
    canvas.end_group();
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::base::input::KeyKind;
    use crate::modules::polkit::model::tests::{Snap, key, prompt};
    use crate::render::recording_canvas::{OpKind, RecordingCanvas};

    fn has_text(canvas: &RecordingCanvas, text: &str) -> bool {
        canvas.ops.iter().any(|op| op.kind == OpKind::Text && op.text == text)
    }

    #[test]
    fn check_polkit_view() {
        let mut idle = Model::default();
        let mut none = RecordingCanvas::default();
        paint(&mut none, &mut idle, 1920.0, 1080.0);
        assert!(none.ops.is_empty(), "a closed model draws nothing");

        let _snap = Snap::on();
        let mut model = Model::default();
        model.sync(true);
        model.set_prompt(prompt(true, "", false));
        let mut canvas = RecordingCanvas::default();
        paint(&mut canvas, &mut model, 1920.0, 1080.0);
        assert!(canvas.ops.first().unwrap().kind == OpKind::BeginGroup && canvas.ops.last().unwrap().kind == OpKind::EndGroup, "everything sits in one card group");
        let group = &canvas.ops[0];
        let card_h = card_height(false);
        assert!(f64::from(group.rect.w) == cfg::CARD_WIDTH && f64::from(group.rect.h) == card_h && group.value == 1.0, "card size and scale");
        assert!(f64::from(group.rect.x) == (1920.0 - cfg::CARD_WIDTH) / 2.0 && group.rect.y == ((1080.0 - card_h) / 2.0) as f32, "card is centered");
        assert!(canvas.count(OpKind::Rounded) == 7 && canvas.count(OpKind::Image) == 0, "card, title tag, their shadows and field, no dots yet");
        assert!(has_text(&canvas, cfg::PASSWORD_PLACEHOLDER), "empty field shows the placeholder");

        for c in ["a", "b", "c"] {
            model.key(&key(KeyKind::Text, c));
        }
        let mut typed = RecordingCanvas::default();
        paint(&mut typed, &mut model, 1920.0, 1080.0);
        assert_eq!(typed.count(OpKind::Image), 3, "one echo image per character");
        assert_eq!(typed.ops[typed.ops.len() - 2].rect.w, cfg::DOT_SIZE as f32, "dots have the configured size");

        for _ in 0..100 {
            model.key(&key(KeyKind::Text, "z"));
        }
        let mut many = RecordingCanvas::default();
        paint(&mut many, &mut model, 1920.0, 1080.0);
        assert_eq!(many.count(OpKind::Image), 25, "dots are limited to what fits");

        let mut fallback = RecordingCanvas { image_id: NO_IMAGE, ..RecordingCanvas::default() };
        paint(&mut fallback, &mut model, 1920.0, 1080.0);
        assert!(fallback.count(OpKind::Image) == 0 && fallback.count(OpKind::Rounded) == 32, "missing echo art falls back to round dots");

        model.set_prompt(prompt(false, "Touch the sensor", false));
        let mut waiting = RecordingCanvas::default();
        paint(&mut waiting, &mut model, 1920.0, 1080.0);
        assert!(has_text(&waiting, cfg::AUTHENTICATING_TEXT) && has_text(&waiting, "Touch the sensor") && f64::from(waiting.ops[0].rect.h) == card_height(true), "waiting state shows the status and the info line");
    }
}
