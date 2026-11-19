//! The desktop menu a right click on the wallpaper opens.

use crate::config::wallpaper_config as cfg;
use crate::render::geometry::Rect;
use crate::render::icons;
use crate::render::tokens::metrics;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DesktopMenuAction {
    Settings,
    Wallpaper,
}

pub struct DesktopMenuEntry {
    pub action: DesktopMenuAction,
    pub label: &'static str,
    pub icon: &'static str,
}

const ENTRIES: [DesktopMenuEntry; 2] = [
    DesktopMenuEntry { action: DesktopMenuAction::Settings, label: "Settings", icon: icons::SETTINGS },
    DesktopMenuEntry { action: DesktopMenuAction::Wallpaper, label: "Wallpaper", icon: icons::PALETTE_ICON },
];

#[derive(Default)]
pub struct DesktopMenuModel {
    rect: Rect,
    hovered: Option<usize>,
    column: usize,
    open: bool,
}

impl DesktopMenuModel {
    pub fn entries() -> &'static [DesktopMenuEntry] {
        &ENTRIES
    }

    /// Opens at `(x, y)`, moved to stay inside the output.
    pub fn open(&mut self, x: f32, y: f32, bounds_width: f32, bounds_height: f32, column: usize) {
        let count = ENTRIES.len() as f32;
        let width = cfg::MENU_WIDTH;
        let height = cfg::MENU_PADDING * 2.0 + count * cfg::MENU_ROW_HEIGHT + (count - 1.0) * cfg::MENU_ROW_GAP + metrics::SHADOW_OFFSET;
        self.rect = Rect {
            x: x.clamp(0.0, (bounds_width - width).max(0.0)),
            y: y.clamp(0.0, (bounds_height - height).max(0.0)),
            w: width,
            h: height,
        };
        self.column = column;
        self.hovered = None;
        self.open = true;
    }

    pub fn close(&mut self) {
        self.open = false;
        self.hovered = None;
    }

    /// Closes an open menu, else opens it; true when it is open after.
    pub fn toggle(&mut self, x: f32, y: f32, bounds_width: f32, bounds_height: f32, column: usize) -> bool {
        if self.open {
            self.close();
        } else {
            self.open(x, y, bounds_width, bounds_height, column);
        }
        self.open
    }

    /// Row `index` in menu coordinates.
    pub fn row(&self, index: usize) -> Rect {
        let top = cfg::MENU_PADDING + index as f32 * (cfg::MENU_ROW_HEIGHT + cfg::MENU_ROW_GAP);
        Rect { x: cfg::MENU_PADDING, y: top, w: cfg::MENU_WIDTH - cfg::MENU_PADDING * 2.0, h: cfg::MENU_ROW_HEIGHT }
    }

    pub fn entry_at(&self, local_x: f32, local_y: f32) -> Option<usize> {
        if !self.open {
            return None;
        }
        (0..ENTRIES.len()).find(|&i| {
            let r = self.row(i);
            local_x >= r.x && local_x < r.x + r.w && local_y >= r.y && local_y < r.y + r.h
        })
    }

    /// True when the hovered row changed.
    pub fn hover(&mut self, local_x: f32, local_y: f32) -> bool {
        let index = self.entry_at(local_x, local_y);
        if index == self.hovered {
            return false;
        }
        self.hovered = index;
        true
    }

    pub fn clear_hover(&mut self) {
        self.hovered = None;
    }

    pub fn is_open(&self) -> bool {
        self.open
    }

    pub fn rect(&self) -> Rect {
        self.rect
    }

    pub fn hovered(&self) -> Option<usize> {
        self.hovered
    }

    pub fn column(&self) -> usize {
        self.column
    }
}
