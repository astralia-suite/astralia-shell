//! The calendar card: today on the left, a pageable month grid on the right.

use crate::base::wall_clock::{format_tm, now_tm};
use crate::config::panel_config as cfg;
use crate::modules::bar::panel::core::panel::{ContentBase, PanelContent, PanelPaint, PanelRegion};
use crate::modules::bar::panel::core::panel_set::PanelAnchor;
use crate::modules::bar::panel::core::widgets;
use crate::render::canvas::{Canvas, FontFamily, TextStyle};
use crate::render::geometry::Rect;
use crate::render::icons;
use crate::render::sticker::{StickerStyle, sticker};
use crate::render::theme::palette;
use crate::render::tokens::{Color, metrics};

const PREVIOUS: i32 = 1;
const TODAY: i32 = 2;
const NEXT: i32 = 3;

const MONTH_NAMES: [&str; 12] = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
const WEEKDAY_NAMES: [&str; 7] = ["Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"];

const LEFT_WIDTH: f32 = (cfg::DATE_WIDTH - 2.0 * cfg::PADDING - cfg::DATE_COLUMN_GAP) / 2.0;
const CELL_SIZE: f32 = (cfg::DATE_WIDTH - 2.0 * cfg::PADDING - LEFT_WIDTH - cfg::DATE_COLUMN_GAP) / 7.0;
const LEFT_HEIGHT: f32 = cfg::DATE_WEEKDAY_LINE + cfg::DATE_LINE_GAP + cfg::DATE_LINE + cfg::DATE_LINE_GAP + cfg::DATE_LINE + cfg::DATE_BIG_DAY_GAP + cfg::DATE_BIG_DAY_ROW + cfg::DATE_BIG_DAY_GAP + cfg::DATE_WEEK_LINE;
const GRID_HEIGHT: f32 = cfg::DATE_GRID_HEADER + cfg::DATE_GRID_HEADER_GAP + cfg::DATE_WEEKDAY_ROW + cfg::DATE_GRID_TOP_GAP + 6.0 * CELL_SIZE;
const TOTAL_HEIGHT: f32 = if LEFT_HEIGHT > GRID_HEIGHT { LEFT_HEIGHT } else { GRID_HEIGHT };

const TODAY_TEXT: Color = Color { r: 1.0, g: 1.0, b: 1.0, a: 1.0 };
const fn style(family: FontFamily, px: i32) -> TextStyle {
    TextStyle { family, px, bold: false, max_width: 0, wrap: false }
}
const TEXT_STYLE: TextStyle = style(FontFamily::Text, cfg::TEXT_PX);
const WEEKDAY_STYLE: TextStyle = style(FontFamily::Text, cfg::DATE_WEEKDAY_PX);
const BIG_STYLE: TextStyle = style(FontFamily::Text, cfg::DATE_BIG_DAY_PX);
const ICON_STYLE: TextStyle = style(FontFamily::Icon, cfg::ICON_PX);

/// One grid cell; `month` counts from 0.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct CalendarDay {
    pub year: i32,
    pub month: i32,
    pub day: i32,
    pub in_month: bool,
}

impl CalendarDay {
    pub fn same_day(&self, year: i32, month: i32, day: i32) -> bool {
        self.year == year && self.month == month && self.day == day
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct CalendarMonth {
    pub year: i32,
    pub month: i32,
}

/// Days since 1970-01-01 for a proleptic Gregorian date; `month` counts from 1.
fn days_from_civil(year: i32, month: i32, day: i32) -> i64 {
    let y = (if month <= 2 { year - 1 } else { year }) as i64;
    let era = y.div_euclid(400);
    let yoe = y - era * 400;
    let m = month as i64;
    let doy = (153 * (if m > 2 { m - 3 } else { m + 9 }) + 2) / 5 + day as i64 - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    era * 146_097 + doe - 719_468
}

/// The date of `days` since 1970-01-01, the month counting from 1.
fn civil_from_days(days: i64) -> (i32, i32, i32) {
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let day = (doy - (153 * mp + 2) / 5 + 1) as i32;
    let month = (if mp < 10 { mp + 3 } else { mp - 9 }) as i32;
    let year = (yoe + era * 400) as i32 + i32::from(month <= 2);
    (year, month, day)
}

/// Monday 0 … Sunday 6.
fn iso_weekday(days: i64) -> i64 {
    (days + 3).rem_euclid(7)
}

/// Six weeks from the Monday on or before the first of `month` (from 0).
pub fn calendar_cells(year: i32, month: i32) -> [CalendarDay; 42] {
    let first = days_from_civil(year, month + 1, 1);
    let start = first - iso_weekday(first);
    std::array::from_fn(|i| {
        let (y, m, d) = civil_from_days(start + i as i64);
        CalendarDay { year: y, month: m - 1, day: d, in_month: m - 1 == month }
    })
}

pub fn month_shifted(year: i32, month: i32, delta: i32) -> CalendarMonth {
    let total = year * 12 + month + delta;
    CalendarMonth { year: total.div_euclid(12), month: total.rem_euclid(12) }
}

/// The ISO 8601 week number of a date, `month` from 0.
pub fn iso_week(year: i32, month: i32, day: i32) -> i32 {
    let date = days_from_civil(year, month + 1, day);
    let thursday = date + 3 - iso_weekday(date);
    let jan1 = days_from_civil(civil_from_days(thursday).0, 1, 1);
    ((thursday - jan1) / 7) as i32 + 1
}

fn nav_button(canvas: &mut dyn Canvas, paint: &mut PanelPaint, x: f32, y: f32, glyph: Option<&str>, action: i32) {
    let p = palette();
    let size = cfg::DATE_NAV_BUTTON;
    let r = Rect { x, y, w: size, h: size };
    sticker(canvas, r, size / 2.0, p.accent_container, &StickerStyle { outline: p.border_outer, border_width: metrics::BORDER_THIN, shadow: 0.0, ..StickerStyle::default() });
    match glyph {
        None => {
            let dot = cfg::DATE_TODAY_DOT;
            canvas.rounded(Rect { x: x + (size - dot) / 2.0, y: y + (size - dot) / 2.0, w: dot, h: dot }, dot / 2.0, p.accent, 0.0, Color::default());
        }
        Some(glyph) => widgets::centered_text(canvas, glyph, &ICON_STYLE, r, p.text),
    }
    paint.region(r, action);
}

#[derive(Default)]
pub struct DatePanel {
    base: ContentBase,
    offset: i32,
}

impl PanelContent for DatePanel {
    fn base(&self) -> &ContentBase {
        &self.base
    }
    fn base_mut(&mut self) -> &mut ContentBase {
        &mut self.base
    }
    fn title(&self) -> &str {
        "Date"
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
        cfg::DATE_WIDTH
    }
    fn closed(&mut self) {
        self.offset = 0;
    }
    fn content_height(&mut self, _canvas: &mut dyn Canvas) -> f32 {
        TOTAL_HEIGHT
    }
    fn paint(&mut self, canvas: &mut dyn Canvas, view: Rect, _scroll: f32, paint: &mut PanelPaint) {
        let p = palette();
        let local = now_tm();
        let (year, month, day) = (local.tm_year + 1900, local.tm_mon, local.tm_mday);
        let shown = month_shifted(year, month, self.offset);

        let mut y = view.y + (TOTAL_HEIGHT - LEFT_HEIGHT) / 2.0;
        let line = |canvas: &mut dyn Canvas, y: f32, text: &str, style: &TextStyle, height: f32, color: Color| {
            widgets::centered_text(canvas, text, style, Rect { x: view.x, y, w: LEFT_WIDTH, h: height }, color);
        };
        line(canvas, y, &format_tm(&local, "%A"), &WEEKDAY_STYLE, cfg::DATE_WEEKDAY_LINE, p.text);
        y += cfg::DATE_WEEKDAY_LINE + cfg::DATE_LINE_GAP;
        line(canvas, y, &format_tm(&local, "%B"), &TEXT_STYLE, cfg::DATE_LINE, p.text_muted);
        y += cfg::DATE_LINE + cfg::DATE_LINE_GAP;
        line(canvas, y, &year.to_string(), &TEXT_STYLE, cfg::DATE_LINE, p.text_muted);
        y += cfg::DATE_LINE + cfg::DATE_BIG_DAY_GAP;
        line(canvas, y, &day.to_string(), &BIG_STYLE, cfg::DATE_BIG_DAY_ROW, p.text);
        y += cfg::DATE_BIG_DAY_ROW + cfg::DATE_BIG_DAY_GAP;
        line(canvas, y, &format!("Week {}", iso_week(year, month, day)), &TEXT_STYLE, cfg::DATE_WEEK_LINE, p.text_dim);

        let grid_x = view.x + LEFT_WIDTH + cfg::DATE_COLUMN_GAP;
        let sample = canvas.measure(WEEKDAY_NAMES[0], &TEXT_STYLE);
        let inset = (CELL_SIZE - sample.w) / 2.0;
        let heading = format!("{} {}", MONTH_NAMES[shown.month as usize], shown.year);
        widgets::text_in_row(canvas, &heading, &TEXT_STYLE, grid_x + inset, Rect { x: grid_x, y: view.y, w: 7.0 * CELL_SIZE, h: cfg::DATE_GRID_HEADER }, p.text);

        let nav = cfg::DATE_NAV_BUTTON;
        let nav_y = view.y + (cfg::DATE_GRID_HEADER - nav) / 2.0;
        let mut nav_x = grid_x + 7.0 * CELL_SIZE - nav;
        nav_button(canvas, paint, nav_x, nav_y, Some(icons::CHEVRON_RIGHT), NEXT);
        nav_x -= nav + cfg::DATE_NAV_GAP;
        nav_button(canvas, paint, nav_x, nav_y, None, TODAY);
        nav_x -= nav + cfg::DATE_NAV_GAP;
        nav_button(canvas, paint, nav_x, nav_y, Some(icons::CHEVRON_LEFT), PREVIOUS);

        let row_y = view.y + cfg::DATE_GRID_HEADER + cfg::DATE_GRID_HEADER_GAP;
        for (col, name) in WEEKDAY_NAMES.iter().enumerate() {
            widgets::centered_text(canvas, name, &TEXT_STYLE, Rect { x: grid_x + col as f32 * CELL_SIZE, y: row_y, w: CELL_SIZE, h: cfg::DATE_WEEKDAY_ROW }, p.text);
        }
        let grid_y = row_y + cfg::DATE_WEEKDAY_ROW + cfg::DATE_GRID_TOP_GAP;
        for (i, entry) in calendar_cells(shown.year, shown.month).iter().enumerate() {
            let cx = grid_x + (i % 7) as f32 * CELL_SIZE;
            let cy = grid_y + (i / 7) as f32 * CELL_SIZE;
            let is_today = entry.same_day(year, month, day);
            if is_today {
                let radius = CELL_SIZE / 2.0 - cfg::DATE_CELL_PADDING;
                let r = Rect { x: cx + CELL_SIZE / 2.0 - radius, y: cy + CELL_SIZE / 2.0 - radius, w: 2.0 * radius, h: 2.0 * radius };
                canvas.rounded(r, radius, p.accent, 0.0, Color::default());
            }
            let color = if is_today { TODAY_TEXT } else if entry.in_month { p.text } else { p.text_dim };
            widgets::centered_text(canvas, &entry.day.to_string(), &TEXT_STYLE, Rect { x: cx, y: cy, w: CELL_SIZE, h: CELL_SIZE }, color);
        }
    }
    fn activate(&mut self, region: &PanelRegion, _x: f64, _y: f64) -> bool {
        self.offset = match region.id {
            PREVIOUS => self.offset - 1,
            NEXT => self.offset + 1,
            _ => 0,
        };
        true
    }
    fn scrollable(&self) -> bool {
        false
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn calendar() {
        let oct = calendar_cells(2026, 9);
        assert!(oct[0].year == 2026 && oct[0].month == 8 && oct[0].day == 28 && !oct[0].in_month, "October 2026 grid starts on Monday 28 September");
        assert!(oct[3].month == 9 && oct[3].day == 1 && oct[3].in_month, "1 October 2026 is a Thursday");
        assert!(oct[41].month == 10 && oct[41].day == 8, "grid ends on 8 November");
        let jun = calendar_cells(2026, 5);
        assert!(jun[0].day == 1 && jun[0].in_month, "a month starting on Monday has no leading days");
        assert!(oct[3].same_day(2026, 9, 1), "same day matches");
        assert!(!oct[0].same_day(2026, 9, 28), "same day needs the month");
        assert_eq!(month_shifted(2026, 0, -1), CalendarMonth { year: 2025, month: 11 }, "January minus one is last December");
        assert_eq!(month_shifted(2026, 11, 13), CalendarMonth { year: 2028, month: 0 }, "December plus 13 is January two years on");
        assert_eq!(iso_week(2026, 0, 1), 1, "1 January 2026 is week 1");
        assert_eq!(iso_week(2027, 0, 1), 53, "1 January 2027 is week 53 of 2026");
        assert_eq!(iso_week(2024, 11, 30), 1, "30 December 2024 is week 1 of 2025");
        assert_eq!(iso_week(2026, 9, 4), 40, "4 October 2026 is week 40");
    }
}
