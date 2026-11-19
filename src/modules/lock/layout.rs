//! Lock card geometry: card size, the three columns, the password dots and the development corner.

use crate::config::lock_config as cfg;
use crate::render::geometry::Rect;
use std::sync::OnceLock;

#[cfg_attr(not(test), expect(dead_code))]
pub fn icon_box_size() -> f32 {
    cfg::FONT_ICON + cfg::ICON_BOX_MARGIN * 4.0
}

pub fn card_height(output_h: f32) -> f32 {
    output_h * cfg::CARD_HEIGHT_MULT
}

pub fn card_width(output_h: f32) -> f32 {
    card_height(output_h) * cfg::CARD_RATIO
}

pub fn center_scale(output_h: f32) -> f32 {
    (output_h / cfg::CENTER_REF_HEIGHT).min(1.0)
}

/// The left, centre and right columns inside the card.
pub fn columns(card_w: f32, card_h: f32, center_w: f32) -> (Rect, Rect, Rect) {
    let inner_x = cfg::PANEL_GAP;
    let inner_y = cfg::PANEL_GAP;
    let inner_h = card_h - 2.0 * cfg::PANEL_GAP;
    let avail = card_w - 4.0 * cfg::PANEL_GAP - center_w;
    let side_w = (avail * 0.5).max(0.0);

    let left = Rect { x: inner_x, y: inner_y, w: side_w, h: inner_h };
    let center = Rect { x: inner_x + side_w + cfg::PANEL_GAP, y: inner_y, w: center_w, h: inner_h };
    let right = Rect { x: center.x + center_w + cfg::PANEL_GAP, y: inner_y, w: side_w, h: inner_h };
    (left, center, right)
}

pub fn side_card_height(column_h: f32) -> f32 {
    ((column_h - cfg::PANEL_GAP) * 0.5).max(0.0)
}

pub fn content_height(clock_h: f32, date_h: f32, message_h: f32) -> f32 {
    clock_h + cfg::GAP_CLOCK_DATE + date_h + cfg::GAP_DATE_AVATAR + cfg::PROFILE_SIZE + cfg::GAP_AVATAR_INPUT + cfg::INPUT_HEIGHT + cfg::GAP_INPUT_MESSAGE + message_h
}

pub fn fetch_colour_count(available_w: f32, max_count: i32) -> i32 {
    let mut n = 0;
    if available_w >= cfg::FETCH_COLOR_BOX {
        n = 1 + ((available_w - cfg::FETCH_COLOR_BOX) / (cfg::FETCH_COLOR_BOX + cfg::FETCH_COLOR_GAP)) as i32;
    }
    n.clamp(0, max_count.max(0))
}

pub fn dot_row_width(count: i32) -> f32 {
    count.max(0) as f32 * cfg::DOT_SIZE
}

pub fn dot_x(index: i32, count: i32, field_width: f32) -> f32 {
    (field_width - dot_row_width(count)) * 0.5 + index as f32 * cfg::DOT_SIZE
}

/// Whether `ASTRALIA_DEBUG_UNLOCK=1` was set when the process started.
pub fn dev_unlock_enabled() -> bool {
    static ENABLED: OnceLock<bool> = OnceLock::new();
    *ENABLED.get_or_init(|| std::env::var("ASTRALIA_DEBUG_UNLOCK").is_ok_and(|v| v == "1"))
}

/// A click in the bottom-left development corner, which unlocks only when enabled.
pub fn dev_unlock_hit(x: f64, y: f64, output_h: f64) -> bool {
    dev_unlock_hit_with(dev_unlock_enabled(), x, y, output_h)
}

fn dev_unlock_hit_with(enabled: bool, x: f64, y: f64, output_h: f64) -> bool {
    enabled && x <= f64::from(cfg::DEV_UNLOCK_WIDTH) && y >= output_h - f64::from(cfg::DEV_UNLOCK_HEIGHT)
}

pub fn panel_origin(output_w: f32, output_h: f32, panel_w: f32, panel_h: f32) -> (f32, f32) {
    ((output_w - panel_w) * 0.5, (output_h - panel_h) * 0.5)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn near(a: f32, b: f32) -> bool {
        (a - b).abs() < 0.01
    }

    #[test]
    fn check_lock_layout() {
        assert!(near(icon_box_size(), cfg::FONT_ICON + cfg::ICON_BOX_MARGIN * 4.0), "lock layout");

        assert!(near(card_height(1440.0), 1440.0 * cfg::CARD_HEIGHT_MULT), "lock layout");
        assert!(near(card_width(1440.0), 1440.0 * cfg::CARD_HEIGHT_MULT * cfg::CARD_RATIO), "lock layout");

        assert!(near(center_scale(1440.0), 1.0), "lock layout");
        assert!(near(center_scale(720.0), 0.5), "lock layout");
        assert!(near(center_scale(2160.0), 1.0), "lock layout");

        let cw = card_width(1440.0);
        let ch = card_height(1440.0);
        let (left, center, right) = columns(cw, ch, cfg::CENTER_WIDTH);
        assert!(near(left.x, cfg::PANEL_GAP), "lock layout");
        assert!(near(center.w, cfg::CENTER_WIDTH), "lock layout");
        assert!(near(left.w, right.w), "lock layout");
        assert!(near(center.x, left.x + left.w + cfg::PANEL_GAP), "lock layout");
        assert!(near(right.x, center.x + center.w + cfg::PANEL_GAP), "lock layout");
        assert!(near(right.x + right.w, cw - cfg::PANEL_GAP), "lock layout");
        assert!(near(left.h, ch - 2.0 * cfg::PANEL_GAP), "lock layout");

        let sc = side_card_height(600.0);
        assert!(near(sc * 2.0 + cfg::PANEL_GAP, 600.0), "lock layout");

        let h = content_height(120.0, 34.0, 20.0);
        assert!(
            near(h, 120.0 + cfg::GAP_CLOCK_DATE + 34.0 + cfg::GAP_DATE_AVATAR + cfg::PROFILE_SIZE + cfg::GAP_AVATAR_INPUT + cfg::INPUT_HEIGHT + cfg::GAP_INPUT_MESSAGE + 20.0),
            "lock layout"
        );

        assert!(dev_unlock_hit_with(true, 0.0, 1200.0, 1200.0) && dev_unlock_hit_with(true, 140.0, 1152.0, 1200.0), "the bottom-left corner unlocks");
        assert!(
            !dev_unlock_hit_with(true, 141.0, 1190.0, 1200.0) && !dev_unlock_hit_with(true, 10.0, 1151.0, 1200.0) && !dev_unlock_hit_with(true, 500.0, 600.0, 1200.0),
            "nothing outside the corner unlocks"
        );
        assert!(!dev_unlock_hit_with(false, 0.0, 1200.0, 1200.0), "the corner is inert unless enabled");

        assert_eq!(fetch_colour_count(0.0, 8), 0, "lock layout");
        assert_eq!(fetch_colour_count(cfg::FETCH_COLOR_BOX, 8), 1, "lock layout");
        assert_eq!(fetch_colour_count(1000.0, 8), 8, "lock layout");

        assert!(near(dot_row_width(3), 3.0 * cfg::DOT_SIZE), "lock layout");
        let w = 400.0;
        let x0 = dot_x(0, 4, w);
        let x1 = dot_x(1, 4, w);
        assert!(near(x1 - x0, cfg::DOT_SIZE), "lock layout");
        assert!(near(x0, (w - 4.0 * cfg::DOT_SIZE) * 0.5), "lock layout");
        assert!(near(dot_row_width(0), 0.0), "lock layout");

        let (px, py) = panel_origin(1920.0, 1080.0, 540.0, 400.0);
        assert!(near(px, (1920.0 - 540.0) * 0.5), "lock layout");
        assert!(near(py, (1080.0 - 400.0) * 0.5), "lock layout");
    }
}
