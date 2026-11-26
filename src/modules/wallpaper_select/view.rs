//! Draws the picker: the carousel of cards over the opened column and the file name under it.

use std::path::Path;

use super::model::Model;
use crate::config::wallpaper_select_config as cfg;
use crate::render::canvas::{Canvas, FontFamily, GroupOptions, ImageId, NO_IMAGE, TextSize, TextStyle};
use crate::render::geometry::Rect;
use crate::render::sticker::{StickerStyle, sticker};
use crate::render::theme::palette;
use crate::render::tokens::{Color, WHITE, metrics};

const LABEL_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::LABEL_PX, bold: true, max_width: 0, wrap: false };

fn lerp(a: f32, b: f32, t: f32) -> f32 {
    a + (b - a) * t
}

fn draw_cover(canvas: &mut dyn Canvas, inner: Rect, radius: f32, image: ImageId, reveal: f32) {
    canvas.begin_group(inner, GroupOptions { clip: true, radius, ..GroupOptions::default() });
    let size = if image == NO_IMAGE { TextSize::default() } else { canvas.image_size(image) };
    if reveal < 1.0 || size.w <= 0.0 || size.h <= 0.0 {
        canvas.rect(Rect { x: 0.0, y: 0.0, w: inner.w, h: inner.h }, palette().surface);
    }
    if size.w > 0.0 && size.h > 0.0 {
        let scale = (inner.w / size.w).max(inner.h / size.h);
        let (w, h) = (size.w * scale, size.h * scale);
        let tint = Color { a: reveal, ..WHITE };
        canvas.draw_image(image, Rect { x: (inner.w - w) / 2.0, y: (inner.h - h) / 2.0, w, h }, tint);
    }
    canvas.end_group();
}

fn draw_card(canvas: &mut dyn Canvas, r: Rect, image: ImageId, reveal: f32) {
    let radius = metrics::RADIUS_CARD;
    let border = metrics::BORDER_THICK;
    sticker(canvas, r, radius, palette().paper, &StickerStyle::default());
    let inner = Rect { x: r.x + border, y: r.y + border, w: (r.w - border * 2.0).max(0.0), h: (r.h - border * 2.0).max(0.0) };
    draw_cover(canvas, inner, (radius - border).max(0.0), image, reveal);
}

fn neighbour_scale(distance: f32) -> f32 {
    lerp(1.0, cfg::NEIGHBOUR_SCALE, distance.min(1.0))
}

/// How far a card `distance` places from the centre sits from it, for a centre card `width` wide.
fn centre_offset(distance: f32, width: f32) -> f32 {
    let near = width / 2.0 + cfg::CARD_GAP + width * cfg::NEIGHBOUR_SCALE / 2.0;
    let side = width * cfg::NEIGHBOUR_SCALE + cfg::CARD_GAP;
    let magnitude = distance.abs();
    let offset = if magnitude <= 1.0 { magnitude * near } else { near + (magnitude - 1.0) * side };
    if distance < 0.0 { -offset } else { offset }
}

fn centred_text(canvas: &mut dyn Canvas, text: &str, cx: f32, y: f32, color: Color) {
    let size = canvas.measure(text, &LABEL_STYLE);
    canvas.text(text, &LABEL_STYLE, cx - size.w / 2.0, y, color);
}

pub fn paint(canvas: &mut dyn Canvas, model: &mut Model, width: f32, height: f32) {
    model.begin_frame();
    canvas.set_opacity(1.0);
    let columns = model.column_count() as f32;
    let x0 = (width * model.column() as f32 / columns).floor();
    let x1 = (width * (model.column() + 1) as f32 / columns).floor();
    let width = x1 - x0;
    let centre_px = cfg::CENTRE_PX.min(width.max(height) as i32);
    let appear = model.appear().clamp(0.0, 1.0);

    if model.files().is_empty() {
        if !model.scanning() {
            canvas.set_opacity(appear);
            centred_text(canvas, &format!("No images in {}", model.directory()), x0 + width / 2.0, height / 2.0, palette().text);
            canvas.set_opacity(1.0);
        }
        return;
    }

    let target_h = height * cfg::CARD_SCALE;
    let stack_h = target_h + cfg::LABEL_GAP + cfg::LABEL_HEIGHT;
    let target_cy = (height - stack_h) / 2.0 + target_h / 2.0;
    let grow = lerp(cfg::APPEAR_SCALE, 1.0, appear);
    let card_w = width * cfg::CARD_SCALE * grow;
    let card_h = target_h * grow;
    let cx = x0 + width / 2.0;
    let cy = target_cy;

    let position = model.position();
    let count = model.files().len() as i32;
    let first = (position.floor() as i32 - cfg::VISIBLE_NEIGHBOURS).max(0);
    let last = (count - 1).min(position.ceil() as i32 + cfg::VISIBLE_NEIGHBOURS);

    // Nearest first, so the decoder starts on what the eye lands on.
    let mut request_order: Vec<i32> = (first..=last).collect();
    request_order.sort_by(|a, b| (*a as f32 - position).abs().total_cmp(&(*b as f32 - position).abs()));
    let settled = (position - model.selected() as f32).abs() < 0.01;
    let mut images = vec![NO_IMAGE; request_order.len()];
    for &index in &request_order {
        let sharp = settled && index as usize == model.selected();
        let path = &model.files()[index as usize];
        images[(index - first) as usize] = canvas.thumbnail(path, if sharp { centre_px } else { cfg::NEIGHBOUR_PX });
    }

    canvas.begin_group(Rect { x: x0, y: 0.0, w: width, h: height }, GroupOptions { clip: true, ..GroupOptions::default() });
    for ring in (0..=cfg::VISIBLE_NEIGHBOURS + 1).rev() {
        for index in first..=last {
            let distance = index as f32 - position;
            if (distance.abs() + 0.5).floor() as i32 != ring {
                continue;
            }
            let scale = neighbour_scale(distance.abs());
            let (w, h) = (card_w * scale, card_h * scale);
            let r = Rect { x: cx + centre_offset(distance, card_w) - w / 2.0, y: cy - h / 2.0, w, h };
            let fade = if distance.abs() <= 1.0 { 1.0 } else { (1.0 - (distance.abs() - 1.0) * cfg::NEIGHBOUR_FADE).max(0.0) };
            canvas.set_opacity(fade * appear);
            let image = images[(index - first) as usize];
            let path = model.files()[index as usize].clone();
            let reveal = model.fade(&path, image != NO_IMAGE);
            draw_card(canvas, Rect { x: r.x - x0, ..r }, image, reveal);
            model.add_card(r, index as usize);
        }
    }
    canvas.set_opacity(1.0);
    canvas.end_group();

    canvas.set_opacity(appear);
    let selected = &model.files()[model.selected()];
    let name = Path::new(selected).file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
    let label = format!("{name}   {} / {}", model.selected() + 1, model.files().len());
    let size = canvas.measure(&label, &LABEL_STYLE);
    let label_w = size.w + cfg::LABEL_PADDING * 2.0;
    let label_y = target_cy + target_h / 2.0 + cfg::LABEL_GAP;
    sticker(canvas, Rect { x: cx - label_w / 2.0, y: label_y, w: label_w, h: cfg::LABEL_HEIGHT }, metrics::RADIUS_WIDGET, palette().paper, &StickerStyle::default());
    canvas.text(&label, &LABEL_STYLE, cx - size.w / 2.0, label_y + (cfg::LABEL_HEIGHT - size.h) / 2.0, palette().text);
    canvas.set_opacity(1.0);
}
