//! Notification card placement, geometry, typography and timings.

use std::time::Duration;

// Placement
pub const MARGIN_RIGHT: i32 = 10;
pub const MARGIN_BOTTOM: i32 = 10;
pub const GAP: f32 = 8.0;
pub const MAX_STACK_HEIGHT: f32 = 480.0;
// Card geometry
pub const CARD_WIDTH: f32 = 400.0;
pub const CARD_PAD: f32 = 16.0;
pub const BORDER_WIDTH: f32 = 2.0;
pub const CONTENT_SPACING: f32 = 10.0;
pub const HEADER_SPACING: f32 = 6.0;
pub const EXTRA_HEIGHT: f32 = 8.0;
pub const URGENCY_DOT: f32 = 6.0;
pub const TAG_PAD: f32 = 6.0;
pub const PROGRESS_HEIGHT: f32 = 4.0;
pub const PROGRESS_TRACK_OPACITY: f32 = 0.3;
// Close button
pub const CLOSE_HIT: f32 = 40.0;
pub const CLOSE_TRAILING_GAP: f32 = 8.0;
pub const CLOSE_ICON_PX: i32 = 16;
pub const CLOSE_IDLE_OPACITY: f32 = 0.4;
pub const WRAP_WIDTH: f32 = CARD_WIDTH - CARD_PAD * 2.0 - (CLOSE_HIT + CLOSE_TRAILING_GAP);
// Typography
pub const APP_PX: i32 = 17;
pub const SUMMARY_PX: i32 = 23;
pub const BODY_PX: i32 = 20;
pub const APP_OPACITY: f32 = 0.65;
pub const BODY_OPACITY: f32 = 0.72;
pub const APP_FALLBACK: &str = "Notification";
// Animation
pub const ANIM_NORMAL_MS: f32 = 220.0;
pub const ANIM_EXIT_BUFFER_MS: f32 = 60.0;
pub const SLIDE_OFFSET: f32 = 24.0;
// Timing
pub const HANG_TIME: Duration = Duration::from_millis(5000);
