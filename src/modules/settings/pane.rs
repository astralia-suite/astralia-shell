//! Drawing helpers for a settings page: every colour fades with the page, every control registers
//! its click region.

use super::model::{Act, Model};
use crate::config::settings::Config;
use crate::config::settings_config as cfg;
use crate::render::canvas::{Canvas, FontFamily, TextSize, TextStyle};
use crate::render::geometry::Rect;
use crate::render::icons;
use crate::render::sticker::{StickerStyle, selectable, sticker};
use crate::render::theme::palette;
use crate::render::tokens::{Color, metrics, with_alpha};

pub const TRANSPARENT: Color = Color { r: 0.0, g: 0.0, b: 0.0, a: 0.0 };
pub const TEXT_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::TEXT_PX, bold: false, max_width: 0, wrap: false };
pub const SMALL_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::SMALL_PX, bold: false, max_width: 0, wrap: false };
pub const ICON_STYLE: TextStyle = TextStyle { family: FontFamily::Icon, px: cfg::ICON_PX, bold: false, max_width: 0, wrap: false };

#[derive(Clone, Debug, PartialEq)]
pub struct DropEntry {
    pub label: String,
    pub a: i32,
    pub b: i32,
}

/// An open drop-down list, drawn over the page once the page is done.
struct DropList {
    anchor: Rect,
    entries: Vec<DropEntry>,
    selected: Option<usize>,
    pick: Act,
}

pub struct Pane<'a> {
    pub canvas: &'a mut dyn Canvas,
    pub model: &'a mut Model,
    /// The config as of this frame.
    pub config: &'a Config,
    pub alpha: f32,
    pub caret: Rect,
    overlay: Option<DropList>,
}

impl<'a> Pane<'a> {
    pub fn new(canvas: &'a mut dyn Canvas, model: &'a mut Model, config: &'a Config, alpha: f32) -> Pane<'a> {
        Pane { canvas, model, config, alpha, caret: Rect::default(), overlay: None }
    }

    pub fn fade(&self, c: Color) -> Color {
        with_alpha(c, c.a * self.alpha)
    }

    pub fn rect(&mut self, r: Rect, c: Color) {
        let c = self.fade(c);
        self.canvas.rect(r, c);
    }

    pub fn rounded(&mut self, r: Rect, radius: f32, fill: Color, border_width: f32, border: Color) {
        let (fill, border) = (self.fade(fill), self.fade(border));
        self.canvas.rounded(r, radius, fill, border_width, border);
    }

    pub fn control(&mut self, r: Rect, radius: f32, fill: Color) {
        let style = StickerStyle { outline: self.fade(palette().border_outer), border_width: metrics::BORDER_THIN, shadow: 0.0, ..StickerStyle::default() };
        let fill = self.fade(fill);
        sticker(self.canvas, r, radius, fill, &style);
    }

    pub fn selectable(&mut self, r: Rect, selected: bool, idle: Color) {
        selectable(self.canvas, r, metrics::RADIUS_WIDGET, selected, false, idle, self.alpha);
    }

    pub fn chip(&mut self, r: Rect, fill: Color) {
        let style = StickerStyle {
            outline: self.fade(palette().border_outer),
            border_width: metrics::BORDER_THIN,
            shadow: metrics::SHADOW_WIDGET,
            ..StickerStyle::default()
        };
        let fill = self.fade(fill);
        sticker(self.canvas, r, metrics::RADIUS_WIDGET, fill, &style);
    }

    pub fn text(&mut self, value: &str, style: &TextStyle, x: f32, y: f32, c: Color) -> TextSize {
        let size = self.canvas.measure(value, style);
        let c = self.fade(c);
        self.canvas.text(value, style, x, y, c);
        size
    }

    pub fn centered(&mut self, value: &str, style: &TextStyle, r: Rect, c: Color) {
        let size = self.canvas.measure(value, style);
        let c = self.fade(c);
        self.canvas.text(value, style, r.x + (r.w - size.w) / 2.0, r.y + (r.h - size.h) / 2.0, c);
    }

    pub fn left(&mut self, value: &str, style: &TextStyle, x: f32, r: Rect, c: Color) {
        let size = self.canvas.measure(value, style);
        let c = self.fade(c);
        self.canvas.text(value, style, x, r.y + (r.h - size.h) / 2.0, c);
    }

    pub fn region(&mut self, r: Rect, act: Act, a: i32, b: i32) {
        self.model.add_region(r, act, a, b);
    }

    /// A labelled row with a drop-down box on its right; an open list is drawn by `finish`.
    #[allow(clippy::too_many_arguments)]
    pub fn dropdown(&mut self, row: Rect, id: i32, label: &str, current: &str, entries: Vec<DropEntry>, selected: Option<usize>, pick: Act) {
        let p = palette();
        self.chip(row, p.surface);
        self.left(label, &TEXT_STYLE, row.x + cfg::TILE_MARGIN, row, with_alpha(p.text, cfg::LABEL_OPACITY));
        let box_w = (row.w * 0.5).min(cfg::DROPDOWN_MAX_WIDTH);
        let r = Rect { x: row.x + row.w - cfg::TILE_MARGIN - box_w, y: row.y + (row.h - cfg::FIELD_HEIGHT) / 2.0, w: box_w, h: cfg::FIELD_HEIGHT };
        let open = self.model.dropdown_open() == Some(id);
        self.selectable(r, open, p.surface);
        self.left(current, &TEXT_STYLE, r.x + cfg::DROPDOWN_PAD, r, p.text);
        let glyph = if open { icons::CHEVRON_UP } else { icons::CHEVRON_DOWN };
        let arrow = self.canvas.measure(glyph, &ICON_STYLE);
        let c = self.fade(p.text);
        self.canvas.text(glyph, &ICON_STYLE, r.x + r.w - cfg::DROPDOWN_PAD - arrow.w, r.y + (r.h - arrow.h) / 2.0, c);
        self.region(r, Act::DropdownToggle, id, 0);
        if open {
            self.overlay = Some(DropList { anchor: r, entries, selected, pick });
        }
    }

    /// Draws an open drop-down list over everything, its rows winning every click.
    pub fn finish(&mut self) {
        let Some(list) = self.overlay.take() else { return };
        let p = palette();
        let row_h = cfg::FIELD_HEIGHT;
        let r = list.anchor;
        let area = Rect { x: r.x, y: r.y + r.h + cfg::DROPDOWN_GAP, w: r.w, h: row_h * list.entries.len() as f32 };
        self.chip(area, p.paper);
        self.model.add_region_front(Rect { x: 0.0, y: 0.0, w: 1.0e5, h: 1.0e5 }, Act::DropdownDismiss, 0, 0);
        for (i, entry) in list.entries.iter().enumerate() {
            let item = Rect { x: area.x, y: area.y + row_h * i as f32, w: area.w, h: row_h };
            if list.selected == Some(i) {
                self.selectable(item, true, p.surface);
            }
            self.left(&entry.label, &TEXT_STYLE, item.x + cfg::DROPDOWN_PAD, item, p.text);
            self.model.add_region_front(item, list.pick, entry.a, entry.b);
        }
    }
}
