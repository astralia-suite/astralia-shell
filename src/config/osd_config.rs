//! OSD surface geometry, typography and timings.

use std::time::Duration;

// Surface
pub const WIDTH: i32 = 300;
pub const HEIGHT: i32 = 50;
pub const MARGIN_BOTTOM: i32 = 30;
// Content
pub const CONTENT_MARGIN: i32 = HEIGHT / 2;
pub const BAR_MARGIN: i32 = 10;
pub const LABEL_WIDTH: i32 = 44;
// Typography
pub const ICON_PX: i32 = 18;
pub const LABEL_PX: i32 = 20;
pub const TRACK_HEIGHT: f32 = 6.0;
pub const BORDER_WIDTH: f32 = 2.0;
// Timing
pub const ANIM_NORMAL_MS: f32 = 220.0;
pub const ANIM_FAST_MS: f32 = 150.0;
pub const VISIBLE_FOR: Duration = Duration::from_millis(2000);
pub const READY_DELAY: Duration = Duration::from_millis(1000);
