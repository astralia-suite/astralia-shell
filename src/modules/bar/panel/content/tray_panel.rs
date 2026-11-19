//! The tray icon grid and, on a right click, the item's menu in a popup.

use std::rc::Rc;

use crate::base::input::{Button, KeyEvent, KeyKind};
use crate::config::panel_config as cfg;
use crate::modules::bar::panel::core::panel::{ContentBase, PanelContent, PanelPaint, PanelRegion};
use crate::modules::bar::panel::core::widgets;
use crate::render::canvas::{Canvas, FontFamily, NO_IMAGE, TextStyle};
use crate::render::geometry::Rect;
use crate::render::icons;
use crate::render::sticker::{StickerStyle, sticker};
use crate::render::theme::palette;
use crate::render::tokens::{Color, WHITE, metrics};
use crate::service::tray::{TrayMenuEntry, TrayService, tray_item_icon_path, tray_strip_mnemonic};

const ITEM: i32 = 1;
const BACK: i32 = 2;
const ENTRY: i32 = 3;
const TEXT_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::TEXT_PX, bold: false, max_width: 0, wrap: false };
const ICON_STYLE: TextStyle = TextStyle { family: FontFamily::Icon, px: cfg::ICON_PX, bold: false, max_width: 0, wrap: false };

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TrayMenuRowKind {
    Back,
    Loading,
    Separator,
    Entry,
}

#[derive(Clone, Copy, Debug)]
pub struct TrayMenuRow<'a> {
    pub kind: TrayMenuRowKind,
    pub height: f32,
    pub entry: Option<&'a TrayMenuEntry>,
}

pub fn tray_columns(width: f32) -> i32 {
    (((width - 2.0 * cfg::PADDING + cfg::TRAY_GAP) / (cfg::TRAY_CELL + cfg::TRAY_GAP)) as i32).max(1)
}

/// The submenu `path` (entry ids) leads to, `None` when the menu is missing or the path is stale.
pub fn tray_menu_level<'a>(root: Option<&'a [TrayMenuEntry]>, path: &[i32]) -> Option<&'a [TrayMenuEntry]> {
    path.iter().try_fold(root?, |level, &id| level.iter().find(|e| e.id == id).map(|e| e.children.as_slice()))
}

pub fn tray_menu_rows(level: Option<&[TrayMenuEntry]>, show_back: bool) -> Vec<TrayMenuRow<'_>> {
    let mut rows = Vec::new();
    if show_back {
        rows.push(TrayMenuRow { kind: TrayMenuRowKind::Back, height: cfg::TRAY_MENU_ROW, entry: None });
    }
    let Some(level) = level else {
        rows.push(TrayMenuRow { kind: TrayMenuRowKind::Loading, height: cfg::TRAY_MENU_ROW, entry: None });
        return rows;
    };
    rows.extend(level.iter().filter(|e| e.visible).map(|e| match e.separator {
        true => TrayMenuRow { kind: TrayMenuRowKind::Separator, height: cfg::TRAY_MENU_SEPARATOR, entry: Some(e) },
        false => TrayMenuRow { kind: TrayMenuRowKind::Entry, height: cfg::TRAY_MENU_ROW, entry: Some(e) },
    }));
    rows
}

fn rows_height(rows: &[TrayMenuRow]) -> f32 {
    rows.iter().map(|r| r.height).sum::<f32>() + rows.len().saturating_sub(1) as f32 * cfg::TRAY_MENU_GAP
}

pub struct TrayPanel {
    base: ContentBase,
    tray: Rc<TrayService>,
    menu_key: String,
    path: Vec<i32>,
    menu_anchor: Rect,
    hover_id: i32,
    hover_a: i32,
}

impl TrayPanel {
    pub fn new(tray: Rc<TrayService>) -> TrayPanel {
        let base = ContentBase::default();
        let notify = base.notifier();
        tray.changed.connect(move |_| notify());
        TrayPanel { base, tray, menu_key: String::new(), path: Vec::new(), menu_anchor: Rect::default(), hover_id: -1, hover_a: -1 }
    }

    fn close_menu(&mut self) {
        self.menu_key.clear();
        self.path.clear();
        self.menu_anchor = Rect::default();
        (self.hover_id, self.hover_a) = (-1, -1);
    }

    /// The open item's whole menu, `None` while it is still loading.
    fn menu(&self) -> Option<Vec<TrayMenuEntry>> {
        if self.menu_key.is_empty() { None } else { self.tray.menu(&self.menu_key) }
    }

    fn back_or_close(&mut self) {
        if self.path.pop().is_none() {
            self.close_menu();
        }
    }

    fn paint_cell(&mut self, canvas: &mut dyn Canvas, cell: Rect, icon_path: &str) {
        let p = palette();
        canvas.rounded(cell, metrics::RADIUS_WIDGET, p.text_alpha06, 0.0, Color::default());
        let image = if icon_path.is_empty() { NO_IMAGE } else { canvas.image(icon_path, cfg::TRAY_ICON as i32) };
        if image == NO_IMAGE {
            let target = Rect { x: cell.x + (cell.w - cfg::TRAY_ICON) / 2.0, y: cell.y + (cell.h - cfg::TRAY_ICON) / 2.0, w: cfg::TRAY_ICON, h: cfg::TRAY_ICON };
            widgets::centered_text(canvas, icons::APPS, &ICON_STYLE, target, p.text);
            return;
        }
        let size = canvas.image_size(image);
        let scale = if size.w > 0.0 && size.h > 0.0 { cfg::TRAY_ICON / size.w.max(size.h) } else { 1.0 };
        let r = Rect { x: cell.x + (cell.w - size.w * scale) / 2.0, y: cell.y + (cell.h - size.h * scale) / 2.0, w: size.w * scale, h: size.h * scale };
        canvas.draw_image(image, r, WHITE);
    }
}

impl PanelContent for TrayPanel {
    fn base(&self) -> &ContentBase {
        &self.base
    }
    fn base_mut(&mut self) -> &mut ContentBase {
        &mut self.base
    }
    fn title(&self) -> &str {
        "Tray"
    }
    fn tint(&self) -> Color {
        palette().accent
    }
    fn closed(&mut self) {
        self.close_menu();
    }
    fn content_height(&mut self, _canvas: &mut dyn Canvas) -> f32 {
        let count = self.tray.items().len() as i32;
        if count == 0 {
            return cfg::TRAY_EMPTY_HEIGHT;
        }
        let columns = tray_columns(self.width());
        let rows = ((count + columns - 1) / columns) as f32;
        rows * cfg::TRAY_CELL + (rows - 1.0) * cfg::TRAY_GAP
    }
    fn paint(&mut self, canvas: &mut dyn Canvas, view: Rect, scroll: f32, paint: &mut PanelPaint) {
        if !self.menu_key.is_empty() && self.tray.find(&self.menu_key).is_none() {
            self.close_menu();
        }
        let icons: Vec<String> = self.tray.items().iter().map(tray_item_icon_path).collect();
        if icons.is_empty() {
            let size = canvas.measure("No tray icons", &TEXT_STYLE);
            canvas.text("No tray icons", &TEXT_STYLE, view.x + (view.w - size.w) / 2.0, view.y - scroll, palette().text_dim);
            return;
        }
        let columns = tray_columns(self.width()) as usize;
        let step = cfg::TRAY_CELL + cfg::TRAY_GAP;
        for (i, icon_path) in icons.iter().enumerate() {
            let cell = Rect { x: view.x + (i % columns) as f32 * step, y: view.y - scroll + (i / columns) as f32 * step, w: cfg::TRAY_CELL, h: cfg::TRAY_CELL };
            if cell.y + cell.h < view.y || cell.y > view.y + view.h {
                continue;
            }
            self.paint_cell(canvas, cell, icon_path);
            paint.push(PanelRegion { rect: cell, id: ITEM, a: i as i32, ..PanelRegion::default() });
        }
    }
    fn popup_anchor(&self) -> Rect {
        self.menu_anchor
    }
    fn popup_width(&self) -> f32 {
        if self.menu_key.is_empty() { 0.0 } else { cfg::TRAY_MENU_WIDTH }
    }
    fn popup_height(&self) -> f32 {
        if self.menu_key.is_empty() || self.tray.find(&self.menu_key).is_none() {
            return 0.0;
        }
        let menu = self.menu();
        rows_height(&tray_menu_rows(tray_menu_level(menu.as_deref(), &self.path), !self.path.is_empty())) + 2.0 * cfg::TRAY_MENU_PAD
    }
    fn paint_popup(&mut self, canvas: &mut dyn Canvas, r: Rect, paint: &mut PanelPaint) {
        let p = palette();
        let menu = self.menu();
        let rows = tray_menu_rows(tray_menu_level(menu.as_deref(), &self.path), !self.path.is_empty());
        sticker(canvas, r, metrics::RADIUS_CARD, p.paper, &StickerStyle::default());
        let x = r.x + cfg::TRAY_MENU_PAD;
        let width = r.w - 2.0 * cfg::TRAY_MENU_PAD;
        let mut y = r.y + cfg::TRAY_MENU_PAD;
        for row in rows {
            let rr = Rect { x, y, w: width, h: row.height };
            y += row.height + cfg::TRAY_MENU_GAP;
            match (row.kind, row.entry) {
                (TrayMenuRowKind::Separator, _) => {
                    let inset = cfg::TRAY_MENU_SEPARATOR_INSET;
                    canvas.rect(Rect { x: rr.x + inset, y: rr.y + rr.h / 2.0, w: rr.w - 2.0 * inset, h: 1.0 }, p.text_alpha06);
                }
                (TrayMenuRowKind::Loading, _) => widgets::text_in_row(canvas, "Loading\u{2026}", &TEXT_STYLE, rr.x + cfg::TRAY_MENU_ROW_PAD, rr, p.text_dim),
                (TrayMenuRowKind::Back, _) => {
                    if self.hover_id == BACK {
                        canvas.rounded(rr, metrics::RADIUS_WIDGET, p.text_alpha08, 0.0, Color::default());
                    }
                    widgets::centered_text(canvas, icons::CHEVRON_LEFT, &ICON_STYLE, Rect { w: cfg::TRAY_MENU_ROW, ..rr }, p.text);
                    paint.region(rr, BACK);
                }
                (TrayMenuRowKind::Entry, Some(e)) => {
                    let color = if e.enabled { p.text } else { p.text_dim };
                    if e.enabled && self.hover_id == ENTRY && self.hover_a == e.id {
                        canvas.rounded(rr, metrics::RADIUS_WIDGET, p.text_alpha08, 0.0, Color::default());
                    }
                    let mut text_x = rr.x + cfg::TRAY_MENU_ROW_PAD;
                    if e.checkbox {
                        let glyph = canvas.measure(icons::CHECK, &ICON_STYLE);
                        if e.checked {
                            canvas.text(icons::CHECK, &ICON_STYLE, text_x, rr.y + (rr.h - glyph.h) / 2.0, color);
                        }
                        text_x += glyph.w + cfg::TRAY_MENU_ROW_PAD;
                    }
                    let label = TextStyle { max_width: ((rr.x + rr.w - text_x - cfg::TRAY_MENU_LABEL_OFFSET) as i32).max(0), ..TEXT_STYLE };
                    widgets::text_in_row(canvas, &tray_strip_mnemonic(&e.label), &label, text_x, rr, color);
                    if !e.children.is_empty() {
                        let glyph = canvas.measure(icons::CHEVRON_RIGHT, &ICON_STYLE);
                        canvas.text(icons::CHEVRON_RIGHT, &ICON_STYLE, rr.x + rr.w - cfg::TRAY_MENU_ROW_PAD - glyph.w, rr.y + (rr.h - glyph.h) / 2.0, p.text_dim);
                    }
                    if e.enabled {
                        paint.push(PanelRegion { rect: rr, id: ENTRY, a: e.id, ..PanelRegion::default() });
                    }
                }
                (TrayMenuRowKind::Entry, None) => {}
            }
        }
    }
    fn popup_hover(&mut self, id: i32, a: i32) -> bool {
        if (id, a) == (self.hover_id, self.hover_a) {
            return false;
        }
        (self.hover_id, self.hover_a) = (id, a);
        true
    }
    fn dismiss_dialog(&mut self) -> bool {
        if self.menu_key.is_empty() {
            return false;
        }
        self.close_menu();
        true
    }
    fn activate(&mut self, region: &PanelRegion, _x: f64, _y: f64) -> bool {
        match region.id {
            ITEM => {
                let Some(target) = usize::try_from(region.a).ok().and_then(|i| self.tray.items().get(i).cloned()) else { return false };
                if self.base.pressed == Button::Right {
                    if !target.has_menu() {
                        self.close_menu();
                        return true;
                    }
                    self.menu_key = target.key();
                    self.menu_anchor = region.rect;
                    self.path.clear();
                    self.tray.request_menu(&self.menu_key);
                    return true;
                }
                self.close_menu();
                self.tray.activate(&target.key(), false);
            }
            BACK => self.back_or_close(),
            ENTRY => {
                let menu = self.menu();
                let Some(e) = tray_menu_level(menu.as_deref(), &self.path).and_then(|level| level.iter().find(|e| e.id == region.a)) else { return true };
                if !e.children.is_empty() {
                    self.path.push(region.a);
                    return true;
                }
                let key = std::mem::take(&mut self.menu_key);
                self.close_menu();
                self.tray.menu_clicked(&key, region.a);
            }
            _ => return false,
        }
        true
    }
    fn key(&mut self, event: &KeyEvent) -> bool {
        if event.kind != KeyKind::Escape || self.menu_key.is_empty() {
            return false;
        }
        self.back_or_close();
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn entry(id: i32, label: &str) -> TrayMenuEntry {
        TrayMenuEntry { id, label: label.into(), enabled: true, visible: true, ..TrayMenuEntry::default() }
    }

    #[test]
    fn tray_menu() {
        let open = entry(1, "Open");
        let gap = TrayMenuEntry { separator: true, ..entry(2, "") };
        let hidden = TrayMenuEntry { visible: false, ..entry(3, "") };
        let more = TrayMenuEntry { children: vec![entry(5, "Child")], ..entry(4, "More") };
        let root = vec![open, gap, hidden, more];

        assert!(tray_menu_level(Some(&root), &[]).is_some_and(|l| std::ptr::eq(l, root.as_slice())), "the empty path is the root");
        let deep = tray_menu_level(Some(&root), &[4]);
        assert!(deep.is_some_and(|d| d.len() == 1 && d[0].id == 5), "a path walks into a submenu");
        assert!(tray_menu_level(Some(&root), &[99]).is_none() && tray_menu_level(None, &[]).is_none(), "a stale path has no level");

        let rows = tray_menu_rows(Some(&root), false);
        assert!(rows.len() == 3 && rows[0].kind == TrayMenuRowKind::Entry && rows[1].kind == TrayMenuRowKind::Separator && rows[1].height < rows[0].height, "hidden entries are skipped and separators are thin");
        let rows = tray_menu_rows(deep, true);
        assert!(rows.len() == 2 && rows[0].kind == TrayMenuRowKind::Back, "a submenu starts with a back row");
        let rows = tray_menu_rows(None, false);
        assert!(rows.len() == 1 && rows[0].kind == TrayMenuRowKind::Loading, "a missing menu is loading");
        assert_eq!(tray_columns(400.0), 8, "the tray grid fits eight columns");
    }
}
