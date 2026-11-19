//! Paints the overview: the scroll behind each block, then the cells, the window tiles and the
//! active-workspace indicator, faded in with the grid.

use super::model::{Cell, Model, Tile};
use crate::config::overview_config as cfg;
use crate::render::canvas::{Canvas, FontFamily, TextStyle};
use crate::render::geometry::Rect;
use crate::render::scroll::{ScrollMetrics, paint_scroll, scroll_geometry};
use crate::render::sticker::{StickerStyle, sticker};
use crate::render::theme::palette;
use crate::render::tokens::{Color, WHITE, metrics, with_alpha};

const TRANSPARENT: Color = Color { r: 0.0, g: 0.0, b: 0.0, a: 0.0 };

fn number_style() -> TextStyle {
    TextStyle { family: FontFamily::Text, px: cfg::NUMBER_PX, ..TextStyle::default() }
}

fn flat(border_width: f32) -> StickerStyle {
    StickerStyle { outline: palette().border_outer, border_width, shadow: 0.0, ..StickerStyle::default() }
}

fn paint_cell(canvas: &mut dyn Canvas, model: &Model, cell: &Cell, scale: f32) {
    let p = palette();
    let hovered = model.dragging() && model.drag_target() == cell.workspace;
    sticker(canvas, cell.rect, metrics::RADIUS_CARD * scale, if hovered { p.pop_a } else { p.surface }, &flat(cfg::WORKSPACE_BORDER_WIDTH));
    let label = cell.workspace.to_string();
    let style = number_style();
    let size = canvas.measure(&label, &style);
    if size.w <= cell.rect.w && size.h <= cell.rect.h {
        canvas.text(&label, &style, cell.rect.x + (cell.rect.w - size.w) / 2.0, cell.rect.y + (cell.rect.h - size.h) / 2.0, with_alpha(p.text, 1.0 - cfg::NUMBER_FADE));
    }
}

fn paint_tile<C: Canvas>(canvas: &mut C, model: &mut Model, tile: &Tile, art: &mut impl FnMut(&mut C, &Tile, Rect, f32) -> bool, scale: f32) {
    let rect = model.tile_rect(tile);
    let radius = metrics::RADIUS_WIDGET * scale;
    if !art(canvas, tile, rect, radius) {
        sticker(canvas, rect, radius, palette().surface_alt, &flat(cfg::WINDOW_BORDER_WIDTH));
    }
    let path = model.icon_path(&tile.window_class);
    if path.is_empty() {
        return;
    }
    let size = (rect.w.min(rect.h) * cfg::CORNER_ICON_RATIO).round();
    if size < 1.0 {
        return;
    }
    let icon = canvas.image(path, size as i32);
    canvas.draw_image(icon, Rect { x: rect.x + rect.w - size - cfg::CORNER_ICON_INSET, y: rect.y + rect.h - size - cfg::CORNER_ICON_INSET, w: size, h: size }, WHITE);
}

/// Paints the open overview; `art` draws a tile's live picture and returns false when it has none,
/// which leaves the placeholder card.
pub fn paint<C: Canvas>(canvas: &mut C, model: &mut Model, art: &mut impl FnMut(&mut C, &Tile, Rect, f32) -> bool) {
    if !model.is_open() || model.layout().cells.is_empty() {
        return;
    }
    let m = ScrollMetrics::default();
    for panel in &model.layout().panels {
        paint_scroll(canvas, &scroll_geometry(*panel, model.scroll(), &m), &m);
    }
    if model.grid_alpha() <= 0.0 {
        return;
    }
    let scale = model.layout().scale;
    canvas.set_opacity(model.grid_alpha());
    for cell in &model.layout().cells {
        paint_cell(canvas, model, cell, scale);
    }
    for tile in model.tiles().to_vec() {
        paint_tile(canvas, model, &tile, art, scale);
    }
    if let Some(indicator) = model.indicator() {
        let p = palette();
        let radius = metrics::RADIUS_CARD * scale;
        let shadow = Rect { y: indicator.y + metrics::SHADOW_WIDGET * scale, ..indicator };
        canvas.rounded(shadow, radius, TRANSPARENT, cfg::INDICATOR_BORDER_WIDTH, p.border_outer);
        canvas.rounded(indicator, radius, TRANSPARENT, cfg::INDICATOR_BORDER_WIDTH, p.accent);
    }
    canvas.set_opacity(1.0);
}
