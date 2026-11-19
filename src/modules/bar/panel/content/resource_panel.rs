//! CPU and GPU gauges (usage with clock, temperature) and the memory and disk bars.

use std::cell::RefCell;
use std::rc::Rc;
use std::time::Duration;

use crate::config::panel_config as cfg;
use crate::modules::bar::panel::core::panel::{ContentBase, PanelContent, PanelPaint};
use crate::modules::bar::panel::core::widgets;
use crate::render::canvas::{Canvas, FontFamily, TextStyle};
use crate::render::geometry::Rect;
use crate::render::sticker::{StickerStyle, sticker};
use crate::render::theme::palette;
use crate::render::tokens::{Color, metrics};
use crate::service::telemetry::{SystemStatsState, Telemetry, cpu_temp_available, gpu_temp_available};

const TEXT_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::TEXT_PX, bold: false, max_width: 0, wrap: false };
const TITLE_STYLE: TextStyle = TextStyle { bold: true, ..TEXT_STYLE };

fn temp_color(celsius: f32, base: Color) -> Color {
    let p = palette();
    if celsius >= cfg::TEMP_CRITICAL {
        p.critical
    } else if celsius >= cfg::TEMP_WARN {
        p.warn
    } else {
        base
    }
}

fn usage_color(value: f32, base: Color) -> Color {
    let p = palette();
    if value >= cfg::USAGE_CRITICAL {
        p.critical
    } else if value >= cfg::USAGE_WARN {
        p.warn
    } else {
        base
    }
}

pub fn format_ghz(ghz: f32) -> String {
    if ghz < 0.0 { "--".to_string() } else { format!("{ghz:.1} GHz") }
}

pub fn format_percent(value01: f32) -> String {
    if value01 < 0.0 { "--".to_string() } else { format!("{}%", (value01 * 100.0) as i32) }
}

pub fn format_celsius(celsius: f32) -> String {
    let value = if celsius < 0.0 { "--".to_string() } else { (celsius as i32).to_string() };
    format!("{value}\u{b0}C")
}

pub fn format_used_cap(used_gb: f32, total_gb: f32) -> String {
    if used_gb < 0.0 || total_gb < 0.0 { "--".to_string() } else { format!("{used_gb:.2} / {total_gb:.2} GB") }
}

fn card_height(content: f32) -> f32 {
    cfg::CARD_TOP + cfg::CARD_HEADER + cfg::CARD_HEADER_GAP + content + cfg::CARD_BOTTOM
}

const DEVICE_CONTENT: f32 = 2.0 * cfg::GAUGE_DIAMETER + cfg::GAUGE_ROW_GAP;

fn line_height(canvas: &mut dyn Canvas) -> f32 {
    canvas.measure("RAM", &TEXT_STYLE).h + cfg::MEMORY_LABEL_GAP + cfg::MEMORY_BAR
}

fn memory_content(canvas: &mut dyn Canvas, stats: &SystemStatsState) -> f32 {
    let lines = if stats.disk_pct >= 0.0 { 2.0 } else { 1.0 };
    lines * line_height(canvas) + (lines - 1.0) * cfg::MEMORY_LINE_GAP
}

/// A card with its title; returns the area under the header.
fn card(canvas: &mut dyn Canvas, r: Rect, title: &str) -> Rect {
    let p = palette();
    sticker(canvas, r, metrics::RADIUS_CARD, p.surface, &StickerStyle { outline: p.border_outer, border_width: metrics::BORDER_THIN, shadow: metrics::SHADOW_WIDGET, ..StickerStyle::default() });
    let size = canvas.measure(title, &TITLE_STYLE);
    canvas.text(title, &TITLE_STYLE, r.x + cfg::CARD_SIDE, r.y + cfg::CARD_TOP + (cfg::CARD_HEADER - size.h) / 2.0, p.text);
    Rect { x: r.x + cfg::CARD_SIDE, y: r.y + cfg::CARD_TOP + cfg::CARD_HEADER + cfg::CARD_HEADER_GAP, w: r.w - 2.0 * cfg::CARD_SIDE, h: r.h }
}

struct Device {
    title: &'static str,
    usage: f32,
    clock_ghz: f32,
    celsius: f32,
    base: Color,
}

fn device_card(canvas: &mut dyn Canvas, r: Rect, d: &Device) {
    let p = palette();
    let inner = card(canvas, r, d.title);
    let gx = inner.x + (inner.w - cfg::GAUGE_DIAMETER) / 2.0;
    let center = TextStyle { max_width: (cfg::GAUGE_DIAMETER - 2.0 * cfg::GAUGE_STROKE) as i32, ..TEXT_STYLE };
    let top = Rect { x: gx, y: inner.y, w: cfg::GAUGE_DIAMETER, h: cfg::GAUGE_DIAMETER };
    canvas.gauge(top, cfg::GAUGE_STROKE, d.usage.max(0.0), usage_color(d.usage, d.base));
    let clock = format_ghz(d.clock_ghz);
    let usage = format_percent(d.usage);
    let clock_size = canvas.measure(&clock, &center);
    let usage_size = canvas.measure(&usage, &center);
    let stack = clock_size.h + cfg::GAUGE_LINE_GAP + usage_size.h;
    let y = top.y + (top.h - stack) / 2.0;
    canvas.text(&clock, &center, top.x + (top.w - clock_size.w) / 2.0, y, p.text_dim);
    canvas.text(&usage, &center, top.x + (top.w - usage_size.w) / 2.0, y + clock_size.h + cfg::GAUGE_LINE_GAP, p.text);
    let bottom = Rect { y: inner.y + cfg::GAUGE_DIAMETER + cfg::GAUGE_ROW_GAP, ..top };
    canvas.gauge(bottom, cfg::GAUGE_STROKE, (d.celsius / cfg::TEMP_GAUGE_MAX).clamp(0.0, 1.0), temp_color(d.celsius, d.base));
    widgets::centered_text(canvas, &format_celsius(d.celsius), &center, bottom, p.text);
}

pub struct ResourcePanel {
    base: ContentBase,
    telemetry: Rc<RefCell<Telemetry>>,
}

impl ResourcePanel {
    pub fn new(telemetry: Rc<RefCell<Telemetry>>) -> ResourcePanel {
        ResourcePanel { base: ContentBase::default(), telemetry }
    }
}

impl PanelContent for ResourcePanel {
    fn base(&self) -> &ContentBase {
        &self.base
    }
    fn base_mut(&mut self) -> &mut ContentBase {
        &mut self.base
    }
    fn title(&self) -> &str {
        "Resource"
    }
    fn tint(&self) -> Color {
        palette().pop_c
    }
    fn max_height(&self) -> f32 {
        cfg::RESOURCE_MAX_HEIGHT
    }
    fn opened(&mut self) {
        self.refresh();
    }
    fn refresh_interval(&self) -> Duration {
        Duration::from_millis(cfg::RESOURCE_POLL_MS as u64)
    }
    fn refresh(&mut self) {
        self.telemetry.borrow_mut().poll();
    }
    fn content_height(&mut self, canvas: &mut dyn Canvas) -> f32 {
        let memory = memory_content(canvas, &self.telemetry.borrow().stats);
        card_height(DEVICE_CONTENT) + cfg::RESOURCE_CARD_GAP + card_height(memory) + cfg::RESOURCE_CARD_GAP + cfg::TRAILING_SPACER
    }
    fn paint(&mut self, canvas: &mut dyn Canvas, view: Rect, scroll: f32, _paint: &mut PanelPaint) {
        let p = palette();
        let t = self.telemetry.borrow();
        let stats = &t.stats;
        let mut y = view.y - scroll;
        let half = (view.w - cfg::RESOURCE_CARD_GAP) / 2.0;
        let devices = card_height(DEVICE_CONTENT);
        let cpu = Device { title: "CPU", usage: stats.cpu_usage, clock_ghz: stats.cpu_freq_ghz, celsius: if cpu_temp_available(&t.cpu) { t.cpu.celsius } else { -1.0 }, base: p.pop_a };
        let gpu_usage = if t.gpu.usage_percent < 0.0 { -1.0 } else { t.gpu.usage_percent / 100.0 };
        let gpu = Device { title: "GPU", usage: gpu_usage, clock_ghz: t.gpu.clock_ghz, celsius: if gpu_temp_available(&t.gpu) { t.gpu.celsius } else { -1.0 }, base: p.accent };
        device_card(canvas, Rect { x: view.x, y, w: half, h: devices }, &cpu);
        device_card(canvas, Rect { x: view.x + half + cfg::RESOURCE_CARD_GAP, y, w: half, h: devices }, &gpu);
        y += devices + cfg::RESOURCE_CARD_GAP;

        let memory = card_height(memory_content(canvas, stats));
        let inner = card(canvas, Rect { x: view.x, y, w: view.w, h: memory }, "Memory");
        let mut lines = vec![("RAM", format_used_cap(stats.mem_used_gb, stats.mem_total_gb), stats.mem_usage, usage_color(stats.mem_usage, p.pop_c))];
        if stats.disk_pct >= 0.0 {
            let disk = (stats.disk_pct / 100.0).clamp(0.0, 1.0);
            lines.push(("Disk", format_used_cap(stats.disk_used_gb, stats.disk_total_gb), disk, usage_color(disk, p.pop_b)));
        }
        let mut line_y = inner.y;
        for (label, detail, value, color) in lines {
            let label_size = canvas.measure(label, &TEXT_STYLE);
            let detail_size = canvas.measure(&detail, &TEXT_STYLE);
            canvas.text(label, &TEXT_STYLE, inner.x, line_y, p.text);
            canvas.text(&detail, &TEXT_STYLE, inner.x + inner.w - detail_size.w, line_y, p.text_dim);
            let bar_y = line_y + label_size.h.max(detail_size.h) + cfg::MEMORY_LABEL_GAP;
            widgets::flat_bar(canvas, Rect { x: inner.x, y: bar_y, w: inner.w, h: cfg::MEMORY_BAR }, value.max(0.0), cfg::MEMORY_MIN_FILL, p.paper, color);
            line_y = bar_y + cfg::MEMORY_BAR + cfg::MEMORY_LINE_GAP;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn resource_levels() {
        let p = palette();
        let base = p.pop_a;
        assert_eq!(usage_color(0.69, base), base);
        assert_eq!(usage_color(cfg::USAGE_WARN, base), p.warn);
        assert_eq!(usage_color(cfg::USAGE_CRITICAL, base), p.critical);
        assert_eq!(usage_color(-1.0, base), base, "an unknown usage keeps the base colour");
        assert_eq!(temp_color(69.9, base), base);
        assert_eq!(temp_color(cfg::TEMP_WARN, base), p.warn);
        assert_eq!(temp_color(cfg::TEMP_CRITICAL, base), p.critical);
        assert_eq!(format_ghz(-1.0), "--");
        assert_eq!(format_ghz(3.456), "3.5 GHz");
        assert_eq!(format_percent(0.426), "42%");
        assert_eq!(format_celsius(-1.0), "--\u{b0}C");
        assert_eq!(format_celsius(54.9), "54\u{b0}C");
        assert_eq!(format_used_cap(3.5, 15.25), "3.50 / 15.25 GB");
        assert_eq!(format_used_cap(-1.0, 15.0), "--");
    }
}
