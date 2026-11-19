//! One output's brightness: a glyph, a slider and the percentage.

use std::cell::Cell;
use std::rc::Rc;

use crate::base::input::{KeyEvent, KeyKind};
use crate::config::panel_config as cfg;
use crate::modules::bar::panel::core::panel::{ContentBase, PanelContent, PanelPaint, PanelRegion};
use crate::modules::bar::panel::core::widgets;
use crate::render::canvas::{Canvas, FontFamily, TextStyle};
use crate::render::geometry::Rect;
use crate::render::glyphs;
use crate::render::theme::palette;
use crate::render::tokens::Color;
use crate::service::brightness::BrightnessService;

const SLIDER_ID: i32 = 1;
const TEXT_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::TEXT_PX, bold: false, max_width: 0, wrap: false };
const ICON_STYLE: TextStyle = TextStyle { family: FontFamily::Icon, px: cfg::ICON_PX, bold: false, max_width: 0, wrap: false };

/// What the panel shows, shared with the service's change handler.
#[derive(Default)]
struct Level {
    percent: Cell<i32>,
    enabled: Cell<bool>,
    dragging: Cell<bool>,
}

impl Level {
    fn sync(&self, brightness: &BrightnessService, output: &str) {
        self.enabled.set(brightness.available_on(output));
        self.percent.set(brightness.percent_on(output));
    }
}

pub struct BrightnessPanel {
    base: ContentBase,
    brightness: Rc<BrightnessService>,
    output: String,
    level: Rc<Level>,
    hovered: bool,
}

impl BrightnessPanel {
    pub fn new(brightness: Rc<BrightnessService>, output: &str) -> BrightnessPanel {
        let base = ContentBase::default();
        let level = Rc::new(Level::default());
        let notify = base.notifier();
        let (weak_level, weak_service, name) = (Rc::downgrade(&level), Rc::downgrade(&brightness), output.to_string());
        brightness.changed.connect(move |_| {
            if let (Some(level), Some(service)) = (weak_level.upgrade(), weak_service.upgrade())
                && !level.dragging.get()
            {
                level.sync(&service, &name);
                notify();
            }
        });
        BrightnessPanel { base, brightness, output: output.to_string(), level, hovered: false }
    }

    pub fn percent(&self) -> i32 {
        self.level.percent.get()
    }

    pub fn enabled(&self) -> bool {
        self.level.enabled.get()
    }

    pub fn apply(&mut self, percent: i32) {
        let percent = percent.clamp(0, 100);
        if percent == self.percent() {
            return;
        }
        self.level.percent.set(percent);
        self.brightness.set_on(&self.output, percent);
    }

    fn apply_at(&mut self, region: &PanelRegion, x: f64) {
        self.apply((widgets::slider_value(region.rect, x) * 100.0).round() as i32);
    }
}

impl PanelContent for BrightnessPanel {
    fn base(&self) -> &ContentBase {
        &self.base
    }
    fn base_mut(&mut self) -> &mut ContentBase {
        &mut self.base
    }
    fn title(&self) -> &str {
        "Brightness"
    }
    fn tint(&self) -> Color {
        palette().pop_b
    }
    fn opened(&mut self) {
        self.level.dragging.set(false);
        self.hovered = false;
        self.brightness.refresh(&self.output);
        self.level.sync(&self.brightness, &self.output);
    }
    fn content_height(&mut self, _canvas: &mut dyn Canvas) -> f32 {
        cfg::BRIGHTNESS_ROW
    }
    fn paint(&mut self, canvas: &mut dyn Canvas, view: Rect, _scroll: f32, paint: &mut PanelPaint) {
        let p = palette();
        let (enabled, percent) = (self.enabled(), self.percent());
        let row = Rect { h: cfg::BRIGHTNESS_ROW, ..view };
        let glyph = glyphs::brightness_threshold(percent);
        let color = if enabled { p.text } else { p.text_dim };
        let icon_size = canvas.measure(glyph, &ICON_STYLE);
        canvas.text(glyph, &ICON_STYLE, row.x, row.y + (row.h - icon_size.h) / 2.0, color);

        let label = if enabled { format!("{percent}%") } else { "-".to_string() };
        let pct_right = row.x + row.w;
        widgets::right_text(canvas, &label, &TEXT_STYLE, pct_right, row, p.text_dim);

        let slider_x = row.x + icon_size.w + cfg::BRIGHTNESS_ICON_GAP;
        let slider_right = pct_right - cfg::BRIGHTNESS_PCT_WIDTH - cfg::BRIGHTNESS_SLIDER_GAP;
        let track = Rect { x: slider_x, y: row.y, w: (slider_right - slider_x).max(0.0), h: row.h };
        let value = if enabled { percent as f32 / 100.0 } else { 0.0 };
        widgets::slider(canvas, track, value, false, enabled && (self.level.dragging.get() || self.hovered), cfg::SLIDER_TRACK);
        if enabled {
            paint.push(PanelRegion { rect: track, id: SLIDER_ID, drag: true, ..PanelRegion::default() });
        }
    }
    fn hover(&mut self, id: i32, _a: i32) -> bool {
        let next = id == SLIDER_ID;
        if next == self.hovered {
            return false;
        }
        self.hovered = next;
        true
    }
    fn activate(&mut self, region: &PanelRegion, x: f64, _y: f64) -> bool {
        self.level.dragging.set(true);
        self.apply_at(region, x);
        true
    }
    fn drag(&mut self, region: &PanelRegion, x: f64, _y: f64) -> bool {
        self.apply_at(region, x);
        true
    }
    fn drop_region(&mut self, _region: &PanelRegion) {
        self.level.dragging.set(false);
        self.level.sync(&self.brightness, &self.output);
    }
    fn key(&mut self, event: &KeyEvent) -> bool {
        if !self.enabled() {
            return false;
        }
        match event.kind {
            KeyKind::Left => self.apply(self.percent() - 1),
            KeyKind::Right => self.apply(self.percent() + 1),
            _ => return false,
        }
        true
    }
    fn wheel(&mut self, _x: f64, _y: f64, dy: f64) -> bool {
        if !self.enabled() || dy == 0.0 {
            return false;
        }
        self.apply(self.percent() + if dy < 0.0 { cfg::BRIGHTNESS_WHEEL_STEP } else { -cfg::BRIGHTNESS_WHEEL_STEP });
        true
    }
    fn scrollable(&self) -> bool {
        false
    }
}
