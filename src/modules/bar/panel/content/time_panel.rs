//! The clock card: the time to the second and the UTC offset.

use std::time::{Duration, SystemTime, UNIX_EPOCH};

use crate::base::wall_clock::{format_tm, now_tm, until_next_second};
use crate::config::panel_config as cfg;
use crate::modules::bar::panel::core::panel::{ContentBase, PanelContent, PanelPaint};
use crate::modules::bar::panel::core::panel_set::PanelAnchor;
use crate::modules::bar::panel::core::widgets;
use crate::render::canvas::{Canvas, FontFamily, TextStyle};
use crate::render::geometry::Rect;
use crate::render::theme::palette;
use crate::render::tokens::Color;

const TOTAL_HEIGHT: f32 = cfg::TIME_BIG_ROW + cfg::TIME_GAP + cfg::TIME_LINE + cfg::TIME_GAP + cfg::TIME_ZONE_LINE;
const SECONDS_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::TIME_SECONDS_PX, bold: false, max_width: 0, wrap: false };
const TEXT_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::TEXT_PX, bold: false, max_width: 0, wrap: false };

fn clock_text(tm: &libc::tm) -> String {
    format_tm(tm, "%H:%M:%S")
}

/// `UTC+7`, `UTC+5:30`, `UTC-3:05`.
fn utc_offset(tm: &libc::tm) -> String {
    let minutes = tm.tm_gmtoff / 60;
    let magnitude = minutes.abs();
    let rest = magnitude % 60;
    let mut text = format!("{}{}", if minutes < 0 { "UTC-" } else { "UTC+" }, magnitude / 60);
    if rest != 0 {
        text += &format!(":{rest:02}");
    }
    text
}

#[derive(Default)]
pub struct TimePanel {
    base: ContentBase,
}

impl PanelContent for TimePanel {
    fn base(&self) -> &ContentBase {
        &self.base
    }
    fn base_mut(&mut self) -> &mut ContentBase {
        &mut self.base
    }
    fn title(&self) -> &str {
        "Time"
    }
    fn tint(&self) -> Color {
        palette().pop_a
    }
    fn anchor(&self) -> PanelAnchor {
        PanelAnchor::Center
    }
    fn has_header(&self) -> bool {
        false
    }
    fn width(&self) -> f32 {
        cfg::TIME_WIDTH
    }
    fn refresh_interval(&self) -> Duration {
        until_next_second(SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().subsec_nanos())
    }
    fn content_height(&mut self, _canvas: &mut dyn Canvas) -> f32 {
        TOTAL_HEIGHT
    }
    fn paint(&mut self, canvas: &mut dyn Canvas, view: Rect, _scroll: f32, _paint: &mut PanelPaint) {
        let tm = now_tm();
        let p = palette();
        let mut y = view.y;
        let mut line = |canvas: &mut dyn Canvas, text: &str, style: &TextStyle, height: f32, color: Color| {
            widgets::centered_text(canvas, text, style, Rect { x: view.x, y, w: view.w, h: height }, color);
            y += height + cfg::TIME_GAP;
        };
        line(canvas, &clock_text(&tm), &SECONDS_STYLE, cfg::TIME_BIG_ROW, p.text);
        line(canvas, &utc_offset(&tm), &TEXT_STYLE, cfg::TIME_ZONE_LINE, p.text_muted);
    }
    fn scrollable(&self) -> bool {
        false
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn time_panel_text() {
        let tm = libc::tm { tm_hour: 7, tm_min: 5, tm_sec: 9, tm_gmtoff: 7 * 3600, ..unsafe { std::mem::zeroed() } };
        assert_eq!(clock_text(&tm), "07:05:09");
        assert_eq!(utc_offset(&tm), "UTC+7");
        assert_eq!(utc_offset(&libc::tm { tm_gmtoff: 5 * 3600 + 30 * 60, ..tm }), "UTC+5:30");
        assert_eq!(utc_offset(&libc::tm { tm_gmtoff: -(3 * 3600 + 5 * 60), ..tm }), "UTC-3:05");
        assert_eq!(utc_offset(&libc::tm { tm_gmtoff: 0, ..tm }), "UTC+0");
    }
}
