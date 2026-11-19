//! Paints the notification cards.

use super::model::{APP_STYLE, BODY_STYLE, Layout, Model, SUMMARY_STYLE, ViewState};
use crate::config::notification_config as cfg;
use crate::render::canvas::{Canvas, FontFamily, GroupOptions, TextStyle};
use crate::render::geometry::Rect;
use crate::render::icons;
use crate::render::sticker::{StickerStyle, sticker};
use crate::render::theme::{Theme, palette};
use crate::render::tokens::{Color, metrics, with_alpha};
use crate::service::notification::NOTIFICATION_URGENCY_CRITICAL;

const CLOSE_STYLE: TextStyle = TextStyle { family: FontFamily::Icon, px: cfg::CLOSE_ICON_PX, bold: false, max_width: 0, wrap: false };

fn urgency_color(p: &Theme, urgency: u8) -> Color {
    match urgency {
        NOTIFICATION_URGENCY_CRITICAL => p.critical,
        0 => p.pop_c,
        _ => p.accent,
    }
}

/// The countdown along the card's top edge: a faint track and the fill shrinking to the right.
fn progress_bar(canvas: &mut dyn Canvas, y: f32, progress: f32, color: Color, opacity: f32) {
    let width = cfg::CARD_WIDTH;
    let rrect_h = 2.0 * metrics::RADIUS_CARD;
    let progress = progress.clamp(0.0, 1.0);
    let clip = GroupOptions { clip: true, ..GroupOptions::default() };
    canvas.begin_group(Rect { x: 0.0, y, w: width, h: cfg::PROGRESS_HEIGHT }, clip);
    let track = with_alpha(color, cfg::PROGRESS_TRACK_OPACITY * opacity);
    canvas.rounded(Rect { x: 0.0, y: 0.0, w: width, h: rrect_h }, metrics::RADIUS_CARD, track, 0.0, track);
    canvas.end_group();
    let fill_x = width * (1.0 - progress);
    canvas.begin_group(Rect { x: fill_x, y, w: width * progress, h: cfg::PROGRESS_HEIGHT }, clip);
    let fill = with_alpha(color, opacity);
    canvas.rounded(Rect { x: -fill_x, y: 0.0, w: width, h: rrect_h }, metrics::RADIUS_CARD, fill, 0.0, fill);
    canvas.end_group();
}

pub fn paint(canvas: &mut dyn Canvas, model: &Model, view: &ViewState, layout: &Layout) {
    let p = palette();
    for card in &layout.cards {
        let Some(entry) = model.entry(card.id) else { continue };
        let opacity = entry.opacity() * view.local_opacity(entry.id);
        let y = card.y + card.slide_offset;
        let accent = urgency_color(&p, entry.urgency);
        let face = if entry.urgency == NOTIFICATION_URGENCY_CRITICAL { p.critical_container } else { p.paper };
        let outline = with_alpha(p.border_outer, opacity);
        let card_style = StickerStyle { outline, ..StickerStyle::default() };
        sticker(canvas, Rect { x: 0.0, y, w: cfg::CARD_WIDTH, h: card.height }, metrics::RADIUS_CARD, with_alpha(face, opacity), &card_style);
        if entry.timed {
            progress_bar(canvas, y, entry.progress(), accent, opacity);
        }

        let x = cfg::CARD_PAD;
        let top = y + cfg::CARD_PAD;
        let app = if entry.app.is_empty() { cfg::APP_FALLBACK } else { entry.app.as_str() };
        let app_size = canvas.measure(app, &APP_STYLE);
        let tag_pad = cfg::TAG_PAD;
        let tag_w = cfg::URGENCY_DOT + cfg::HEADER_SPACING + app_size.w + 2.0 * tag_pad;
        let tag_h = app_size.h + tag_pad;
        let tag_style = StickerStyle { outline, border_width: metrics::BORDER_THIN, shadow: metrics::SHADOW_WIDGET, ..StickerStyle::default() };
        let tag = Rect { x: x - tag_pad, y: top + (card.header - tag_h) / 2.0, w: tag_w, h: tag_h + metrics::SHADOW_WIDGET };
        sticker(canvas, tag, metrics::RADIUS_WIDGET, with_alpha(accent, opacity), &tag_style);
        let dot = Rect { x, y: top + (card.header - cfg::URGENCY_DOT) / 2.0, w: cfg::URGENCY_DOT, h: cfg::URGENCY_DOT };
        canvas.rounded(dot, cfg::URGENCY_DOT / 2.0, outline, 0.0, outline);
        canvas.text(app, &APP_STYLE, x + cfg::URGENCY_DOT + cfg::HEADER_SPACING, top + (card.header - app_size.h) / 2.0, with_alpha(p.text, opacity));

        let mut row = top + card.header;
        if card.summary > 0.0 {
            row += cfg::CONTENT_SPACING;
            canvas.text(&entry.summary, &SUMMARY_STYLE, x, row, with_alpha(p.text, opacity));
            row += card.summary;
        }
        if card.body > 0.0 {
            row += cfg::CONTENT_SPACING;
            canvas.text(&entry.body, &BODY_STYLE, x, row, with_alpha(p.text, cfg::BODY_OPACITY * opacity));
        }

        let glyph = canvas.measure(icons::CLOSE, &CLOSE_STYLE);
        let close_x = cfg::CARD_WIDTH - cfg::CLOSE_HIT;
        let close_opacity = opacity * if view.hovered() == Some(entry.id) { 1.0 } else { cfg::CLOSE_IDLE_OPACITY };
        let gx = (close_x + (cfg::CLOSE_HIT - glyph.w) / 2.0).round();
        canvas.text(icons::CLOSE, &CLOSE_STYLE, gx, y + ((cfg::CLOSE_HIT - glyph.h) / 2.0).round(), with_alpha(p.text, close_opacity));
    }
}
