//! Paints the desktop menu.

use super::model::DesktopMenuModel;
use crate::config::wallpaper_config as cfg;
use crate::render::canvas::{Canvas, FontFamily, TextStyle};
use crate::render::geometry::Rect;
use crate::render::sticker::{StickerStyle, selectable, sticker};
use crate::render::theme::palette;
use crate::render::tokens::metrics;

const ICON_STYLE: TextStyle = TextStyle { family: FontFamily::Icon, px: cfg::MENU_ICON_SIZE as i32, bold: false, max_width: 0, wrap: false };
const LABEL_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::MENU_TEXT_PX, bold: true, max_width: 0, wrap: false };

/// The menu card with its top-left corner at `(x, y)`; nothing while closed.
pub fn paint(canvas: &mut dyn Canvas, model: &DesktopMenuModel, x: f32, y: f32) {
    if !model.is_open() {
        return;
    }
    canvas.set_opacity(1.0);
    let p = palette();
    let card = model.rect();
    sticker(canvas, Rect { x, y, ..card }, metrics::RADIUS_CARD, p.paper, &StickerStyle::default());
    for (i, entry) in DesktopMenuModel::entries().iter().enumerate() {
        let mut row = model.row(i);
        row.x += x;
        row.y += y;
        selectable(canvas, row, metrics::RADIUS_WIDGET, false, model.hovered() == Some(i), p.surface, 1.0);
        let icon_size = canvas.measure(entry.icon, &ICON_STYLE);
        let label_size = canvas.measure(entry.label, &LABEL_STYLE);
        let icon_x = row.x + cfg::MENU_PADDING + 2.0;
        canvas.text(entry.icon, &ICON_STYLE, icon_x, row.y + (row.h - icon_size.h) / 2.0, p.text);
        canvas.text(entry.label, &LABEL_STYLE, icon_x + cfg::MENU_ICON_SIZE + cfg::MENU_ICON_GAP, row.y + (row.h - label_size.h) / 2.0, p.text);
    }
}
