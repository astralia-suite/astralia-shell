//! Every battery UPower reports, with its state, time left and charge bar.

use std::rc::Rc;

use crate::config::panel_config as cfg;
use crate::modules::bar::panel::core::panel::{ContentBase, PanelContent, PanelPaint};
use crate::modules::bar::panel::core::widgets;
use crate::render::canvas::{Canvas, FontFamily, TextStyle};
use crate::render::geometry::Rect;
use crate::render::theme::palette;
use crate::render::tokens::Color;
use crate::service::battery::{BatteryDevice, BatteryService, UPOWER_STATE_CHARGING, UPOWER_STATE_DISCHARGING, UPOWER_STATE_FULLY_CHARGED, UPOWER_STATE_PENDING_CHARGE, UPOWER_TYPE_BATTERY};

const TEXT_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::TEXT_PX, bold: false, max_width: 0, wrap: false };

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BatteryRowKind {
    EmptyPluggedIn,
    EmptyNoBattery,
    Device,
    Spacer,
}

#[derive(Clone, Copy, Debug)]
pub struct BatteryRow<'a> {
    pub kind: BatteryRowKind,
    pub height: f32,
    pub device: Option<&'a BatteryDevice>,
}

/// `2h 5m`, `45m`, or empty when unknown.
pub fn battery_time_text(seconds: i32) -> String {
    if seconds <= 0 {
        return String::new();
    }
    let (hours, minutes) = (seconds / 3600, seconds % 3600 / 60);
    if hours > 0 { format!("{hours}h {minutes}m") } else { format!("{minutes}m") }
}

pub fn battery_rows(devices: &[BatteryDevice], on_battery: bool) -> Vec<BatteryRow<'_>> {
    let row = |kind, height, device| BatteryRow { kind, height, device };
    let matched: Vec<&BatteryDevice> = devices.iter().filter(|d| d.is_battery() && d.present).collect();
    let detected = devices.iter().any(|d| d.kind == UPOWER_TYPE_BATTERY && d.present);
    let all_full = !on_battery && matched.iter().all(|d| d.state == UPOWER_STATE_FULLY_CHARGED);
    let mut rows = Vec::new();
    if detected && all_full {
        rows.push(row(BatteryRowKind::EmptyPluggedIn, cfg::BATTERY_EMPTY, None));
    }
    if !all_full {
        rows.extend(matched.into_iter().map(|d| row(BatteryRowKind::Device, cfg::BATTERY_ROW, Some(d))));
    }
    if !detected {
        rows.push(row(BatteryRowKind::EmptyNoBattery, cfg::BATTERY_EMPTY, None));
    }
    rows.push(row(BatteryRowKind::Spacer, cfg::TRAILING_SPACER, None));
    rows
}

pub struct BatteryPanel {
    base: ContentBase,
    battery: Rc<BatteryService>,
}

impl BatteryPanel {
    pub fn new(battery: Rc<BatteryService>) -> BatteryPanel {
        let base = ContentBase::default();
        let notify = base.notifier();
        battery.changed.connect(move |_| notify());
        BatteryPanel { base, battery }
    }
}

fn paint_device(canvas: &mut dyn Canvas, r: Rect, device: &BatteryDevice, on_battery: bool) {
    let p = palette();
    let charging = device.state == UPOWER_STATE_CHARGING;
    let full = device.state == UPOWER_STATE_FULLY_CHARGED;
    let pending = device.state == UPOWER_STATE_PENDING_CHARGE || (device.state == UPOWER_STATE_DISCHARGING && !on_battery);
    let bar_color = if charging || full {
        p.accent
    } else if device.percent <= 15 {
        p.critical
    } else if device.percent <= 30 {
        p.warn
    } else {
        p.text_muted
    };
    let name = if device.native_path.is_empty() { "Battery" } else { &device.native_path };
    let state = if charging {
        "Charging"
    } else if full {
        "Full"
    } else if pending {
        "Pending"
    } else {
        "Discharging"
    };
    let time = battery_time_text(if charging { device.time_to_full_s } else { device.time_to_empty_s });
    let text_row = Rect { h: cfg::BATTERY_TEXT_ROW, ..r };
    let name_size = canvas.measure(name, &TEXT_STYLE);
    widgets::text_in_row(canvas, name, &TEXT_STYLE, r.x, text_row, p.text);
    let status = if time.is_empty() { state.to_string() } else { format!("{state} \u{2014} {time}") };
    widgets::text_in_row(canvas, &status, &TEXT_STYLE, r.x + name_size.w + cfg::TIGHT_GAP, text_row, if charging { p.accent } else { p.text_dim });

    let percent = format!("{}%", device.percent);
    let pct_size = canvas.measure(&percent, &TEXT_STYLE);
    let bar_y = r.y + cfg::BATTERY_TEXT_ROW + cfg::BATTERY_BAR_TOP_GAP;
    let bar_w = r.w - pct_size.w - cfg::CONTENT_GAP;
    let bar = Rect { x: r.x, y: bar_y, w: bar_w, h: cfg::BATTERY_BAR_HEIGHT };
    widgets::flat_bar(canvas, bar, device.percent.min(100) as f32 / 100.0, cfg::BATTERY_BAR_HEIGHT, p.text_alpha08, bar_color);
    canvas.text(&percent, &TEXT_STYLE, r.x + bar_w + cfg::CONTENT_GAP, bar_y + (cfg::BATTERY_BAR_HEIGHT - pct_size.h) / 2.0, bar_color);
}

impl PanelContent for BatteryPanel {
    fn base(&self) -> &ContentBase {
        &self.base
    }
    fn base_mut(&mut self) -> &mut ContentBase {
        &mut self.base
    }
    fn title(&self) -> &str {
        "Battery"
    }
    fn tint(&self) -> Color {
        palette().pop_b
    }
    fn content_height(&mut self, _canvas: &mut dyn Canvas) -> f32 {
        let devices = self.battery.devices();
        let rows = battery_rows(&devices, self.battery.on_battery());
        rows.iter().map(|r| r.height).sum::<f32>() + cfg::LIST_SPACING * rows.len().saturating_sub(1) as f32
    }
    fn paint(&mut self, canvas: &mut dyn Canvas, view: Rect, scroll: f32, _paint: &mut PanelPaint) {
        let on_battery = self.battery.on_battery();
        let devices = self.battery.devices();
        let mut y = view.y - scroll;
        for (i, row) in battery_rows(&devices, on_battery).iter().enumerate() {
            if i > 0 {
                y += cfg::LIST_SPACING;
            }
            let r = Rect { x: view.x, y, w: view.w, h: row.height };
            y += row.height;
            if r.y + r.h < view.y || r.y > view.y + view.h {
                continue;
            }
            match (row.kind, row.device) {
                (BatteryRowKind::EmptyPluggedIn, _) => widgets::centered_text(canvas, "Plugged in", &TEXT_STYLE, r, palette().text_dim),
                (BatteryRowKind::EmptyNoBattery, _) => widgets::centered_text(canvas, "No Battery Detected", &TEXT_STYLE, r, palette().text_dim),
                (BatteryRowKind::Device, Some(device)) => paint_device(canvas, r, device, on_battery),
                _ => {}
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn battery_text() {
        assert!(battery_time_text(0).is_empty(), "unknown time is blank");
        assert_eq!(battery_time_text(45 * 60), "45m", "minutes only");
        assert_eq!(battery_time_text(2 * 3600 + 5 * 60), "2h 5m", "hours and minutes");
    }
}
