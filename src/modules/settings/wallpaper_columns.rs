//! The wallpaper controls of a page: the column drop-down, the folder bar, the column stepper and
//! fill mode, and the thumbnail grid.

use std::path::Path;

use super::model::{Act, Field, Model};
use super::pane::{DropEntry, Pane, SMALL_STYLE, TEXT_STYLE, TRANSPARENT};
use crate::config::file::collapse_home;
use crate::config::settings_config as cfg;
use crate::render::canvas::{GroupOptions, NO_IMAGE};
use crate::render::field_view::{FieldDraw, draw_field_input};
use crate::render::geometry::Rect;
use crate::render::theme::palette;
use crate::render::tokens::{WHITE, metrics};
use crate::service::wallpaper::{
    WallpaperSlot, wallpaper_column_count, wallpaper_column_override, wallpaper_column_path, wallpaper_fill_mode, wallpaper_slot_dir,
};

/// One monitor column a wallpaper can be set on.
#[derive(Clone, Debug, PartialEq)]
pub struct WallpaperChip {
    pub monitor: usize,
    pub column: i32,
    pub label: String,
}

fn dir_field_for(slot: WallpaperSlot) -> Field {
    match slot {
        WallpaperSlot::Animated => Field::WallpaperAnimatedDir,
        WallpaperSlot::Lock => Field::LockWallpaperDir,
        WallpaperSlot::Still => Field::WallpaperDir,
    }
}

/// Every column of every monitor; a monitor with one column is named alone.
pub fn wallpaper_chips(model: &Model, slot: WallpaperSlot) -> Vec<WallpaperChip> {
    let config = model.config();
    let mut chips = Vec::new();
    for (m, name) in model.monitors().iter().enumerate() {
        let count = wallpaper_column_count(&config, name, slot);
        for column in 0..count {
            let label = if count > 1 { format!("{name}-{}", column + 1) } else { name.clone() };
            chips.push(WallpaperChip { monitor: m, column, label });
        }
    }
    chips
}

/// Draws the controls from `y` down, the grid ending by `bottom`; `display` puts the drop-down
/// there instead of on its own row. Returns the bottom of what was drawn.
#[allow(clippy::too_many_arguments)]
pub fn wallpaper_columns_panel(pane: &mut Pane, slot: WallpaperSlot, x: f32, mut y: f32, w: f32, bottom: f32, display: Option<Rect>) -> f32 {
    let p = palette();
    let config = pane.config;
    let animated = slot == WallpaperSlot::Animated;
    let slot_flag = slot as i32;
    pane.model.picker_mut(slot).grid_width = w;
    let (region, column) = {
        let picker = pane.model.picker(slot);
        (picker.region.clone(), picker.column)
    };

    let chips = wallpaper_chips(pane.model, slot);
    let mut entries = Vec::new();
    let mut active = None;
    for chip in &chips {
        if pane.model.monitors()[chip.monitor] == region && chip.column == column {
            active = Some(entries.len());
        }
        entries.push(DropEntry { label: chip.label.clone(), a: chip.monitor as i32, b: chip.column | (slot_flag << 8) });
    }
    let current = active.map(|i| entries[i].label.clone()).unwrap_or_default();
    let row = display.unwrap_or(Rect { x, y, w, h: cfg::TILE_HEIGHT });
    pane.dropdown(row, 4, "Display", &current, entries, active, Act::WallpaperRegion);
    if display.is_none() {
        y += cfg::TILE_HEIGHT + cfg::ROW_GAP;
    }

    let dir_field = dir_field_for(slot);
    let focused = pane.model.focused() == dir_field;
    let bar = Rect { x, y, w, h: cfg::DIR_BAR_HEIGHT };
    pane.control(bar, metrics::RADIUS_WIDGET, if focused { p.pop_b } else { p.field_bg });
    let mut input_x = x + cfg::DIR_BAR_LABEL_MARGIN;
    let dir_h = pane.canvas.measure("Dir", &TEXT_STYLE).h;
    input_x += pane.text("Dir", &TEXT_STYLE, input_x, y + (cfg::DIR_BAR_HEIGHT - dir_h) / 2.0, p.lavender).w + cfg::DIR_BAR_FIELD_MARGIN;
    let button_x = x + w - cfg::DIR_BAR_EDGE_MARGIN - cfg::DIR_BAR_BUTTON_WIDTH;
    let input_w = button_x - cfg::DIR_BAR_EDGE_MARGIN - input_x;
    let center_y = y + cfg::DIR_BAR_HEIGHT / 2.0;
    if focused {
        let draw = FieldDraw {
            x: input_x,
            center_y,
            caret_height: cfg::DIR_BAR_HEIGHT - 16.0,
            caret_width: 1.5,
            caret_gap: 2.0,
            color: pane.fade(p.text),
            ..FieldDraw::new(TEXT_STYLE)
        };
        let model = &*pane.model;
        pane.caret = draw_field_input(pane.canvas, &model.field().text, model.field(), model.field_anim(), &draw, None);
    } else {
        let clipped = crate::render::canvas::TextStyle { max_width: input_w as i32, ..TEXT_STYLE };
        let home = std::env::var("HOME").unwrap_or_default();
        let shown = collapse_home(wallpaper_slot_dir(config, slot), &home);
        let size = pane.canvas.measure(&shown, &clipped);
        pane.text(&shown, &clipped, input_x, y + (cfg::DIR_BAR_HEIGHT - size.h) / 2.0, p.text);
    }
    pane.region(Rect { x: input_x, y, w: input_w, h: cfg::DIR_BAR_HEIGHT }, Act::FieldFocus, dir_field as i32, 0);
    let button = Rect { x: button_x, y: y + (cfg::DIR_BAR_HEIGHT - cfg::DIR_BAR_BUTTON_HEIGHT) / 2.0, w: cfg::DIR_BAR_BUTTON_WIDTH, h: cfg::DIR_BAR_BUTTON_HEIGHT };
    pane.selectable(button, false, p.pop_b);
    let scanning = pane.model.picker(slot).scanning;
    pane.centered(if scanning { "\u{2026}" } else { "Rescan" }, &TEXT_STYLE, button, p.text);
    pane.region(button, Act::WallpaperRescan, 0, slot_flag);
    y += cfg::DIR_BAR_HEIGHT + cfg::ROW_GAP;

    let own = wallpaper_column_override(config, &region, column, slot);
    let control_y = y;
    let count = if region.is_empty() { 1 } else { wallpaper_column_count(config, &region, slot) };
    let minus = Rect { x, y: control_y, w: cfg::STEPPER_BUTTON, h: cfg::FIELD_HEIGHT };
    pane.selectable(minus, false, p.surface);
    pane.centered("-", &TEXT_STYLE, minus, p.text);
    pane.region(minus, Act::WallpaperCount, -1, slot_flag);
    let count_text = count.to_string();
    let count_w = pane.canvas.measure(&count_text, &TEXT_STYLE).w + 12.0;
    pane.centered(&count_text, &TEXT_STYLE, Rect { x: x + cfg::STEPPER_BUTTON, y: control_y, w: count_w, h: cfg::FIELD_HEIGHT }, p.text);
    let plus = Rect { x: x + cfg::STEPPER_BUTTON + count_w, y: control_y, w: cfg::STEPPER_BUTTON, h: cfg::FIELD_HEIGHT };
    pane.selectable(plus, false, p.surface);
    pane.centered("+", &TEXT_STYLE, plus, p.text);
    pane.region(plus, Act::WallpaperCount, 1, slot_flag);

    let mode = wallpaper_fill_mode(config, &region, column, slot);
    let labels = ["Crop", "Fit"];
    let widths = labels.map(|l| pane.canvas.measure(l, &TEXT_STYLE).w + 20.0);
    let pair = widths[0] + widths[1] + 6.0;
    let mut fx = x + w - pair;
    for (i, label) in labels.iter().enumerate() {
        let active = mode == if i == 0 { "crop" } else { "fit" };
        let r = Rect { x: fx, y: control_y, w: widths[i], h: cfg::FIELD_HEIGHT };
        pane.selectable(r, active, p.surface);
        pane.centered(label, &TEXT_STYLE, r, p.text);
        pane.region(r, Act::WallpaperFill, i as i32, slot_flag);
        fx += widths[i] + 6.0;
    }
    if !own.is_empty() {
        let label = "Remove";
        let rw = pane.canvas.measure(label, &TEXT_STYLE).w + 20.0;
        let rx = x + (w - rw) / 2.0;
        let r = Rect { x: rx, y: control_y, w: rw, h: cfg::FIELD_HEIGHT };
        pane.chip(r, p.critical_container);
        pane.centered(label, &TEXT_STYLE, r, p.text);
        pane.region(r, Act::WallpaperRemove, 0, slot_flag);
    }
    y += cfg::ROW_HEIGHT;

    let available = bottom - y;
    let inset_w = w - cfg::GRID_INSET * 2.0;
    let columns = cfg::THUMB_COLUMNS as usize;
    let cell = cfg::THUMB_SIZE + cfg::THUMB_GAP;
    let files = pane.model.picker(slot).files.clone();
    let rows = files.len().div_ceil(columns);
    let content_h = if rows == 0 { 0.0 } else { rows as f32 * cell - cfg::THUMB_GAP };
    let grid_height = available.min(content_h + cfg::GRID_INSET * 2.0);
    pane.model.picker_mut(slot).grid_height = grid_height;
    if files.is_empty() {
        let text = if scanning {
            "Scanning\u{2026}"
        } else if animated {
            "No videos found"
        } else {
            "No images found"
        };
        let size = pane.text(text, &TEXT_STYLE, x, y, p.text_dim);
        return y + size.h;
    }
    pane.rounded(Rect { x, y, w, h: grid_height }, metrics::RADIUS_WIDGET, TRANSPARENT, metrics::BORDER_THIN, p.border_outer);
    let visible_h = (grid_height - cfg::GRID_INSET * 2.0).min(content_h);
    let row_w = columns as f32 * cell - cfg::THUMB_GAP;
    let grid_x = x + cfg::GRID_INSET + (inset_w - row_w) / 2.0;
    let grid_y = y + cfg::GRID_INSET;
    let scroll = {
        let picker = pane.model.picker_mut(slot);
        picker.scroll = picker.scroll.clamp(0.0, (content_h - visible_h).max(0.0));
        picker.scroll
    };
    let selected = wallpaper_column_path(config, &region, column, slot);

    let clip = GroupOptions { clip: true, ..GroupOptions::default() };
    pane.canvas.begin_group(Rect { x: grid_x, y: grid_y, w: row_w, h: visible_h }, clip);
    let last_index = rows.saturating_sub(1) as i32;
    let first_row = ((scroll / cell) as i32).clamp(0, last_index);
    let last_row = (((scroll + visible_h) / cell) as i32).clamp(0, last_index);
    for row in first_row..=last_row {
        for col in 0..columns {
            let index = row as usize * columns + col;
            let Some(path) = files.get(index) else { break };
            let tile = Rect { x: col as f32 * cell, y: row as f32 * cell - scroll, w: cfg::THUMB_SIZE, h: cfg::THUMB_SIZE };
            let active = *path == selected;
            pane.rounded(tile, metrics::RADIUS_WIDGET, p.field_bg, 0.0, TRANSPARENT);
            let image = pane.canvas.thumbnail(path, cfg::THUMB_SIZE as i32);
            if image != NO_IMAGE {
                let size = pane.canvas.image_size(image);
                if size.w > 0.0 && size.h > 0.0 {
                    let scale = (cfg::THUMB_SIZE / size.w).max(cfg::THUMB_SIZE / size.h);
                    let tint = pane.fade(WHITE);
                    pane.canvas.begin_group(tile, clip);
                    let r = Rect {
                        x: (cfg::THUMB_SIZE - size.w * scale) / 2.0,
                        y: (cfg::THUMB_SIZE - size.h * scale) / 2.0,
                        w: size.w * scale,
                        h: size.h * scale,
                    };
                    pane.canvas.draw_image(image, r, tint);
                    pane.canvas.end_group();
                }
            }
            let clipped = crate::render::canvas::TextStyle { max_width: (cfg::THUMB_SIZE - cfg::THUMB_LABEL_PAD) as i32, ..SMALL_STYLE };
            let name = Path::new(path).file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
            let name_size = pane.canvas.measure(&name, &clipped);
            let label_h = name_size.h + cfg::THUMB_LABEL_PAD;
            pane.rect(Rect { x: tile.x, y: tile.y + cfg::THUMB_SIZE - label_h, w: cfg::THUMB_SIZE, h: label_h }, p.overlay);
            pane.text(&name, &clipped, tile.x + cfg::THUMB_LABEL_PAD / 2.0, tile.y + cfg::THUMB_SIZE - (label_h + name_size.h) / 2.0, p.text);
            let (border, border_color) = if active { (metrics::BORDER_THICK, p.border_outer) } else { (0.0, TRANSPARENT) };
            pane.rounded(tile, metrics::RADIUS_WIDGET, TRANSPARENT, border, border_color);
            let hit = Rect { x: grid_x + tile.x, y: grid_y + tile.y, w: tile.w, h: tile.h };
            let top = hit.y.max(grid_y);
            let bottom_edge = (hit.y + hit.h).min(grid_y + visible_h);
            if bottom_edge > top {
                pane.region(Rect { x: hit.x, y: top, w: hit.w, h: bottom_edge - top }, Act::WallpaperPick, index as i32, slot_flag);
            }
        }
    }
    pane.canvas.end_group();
    y + grid_height
}
