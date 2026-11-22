//! Bar geometry, sizes and animation timings.

use std::time::Duration;

pub const HEIGHT: i32 = 40;
pub const TOP_MARGIN: i32 = 10;
pub const SIDE_MARGIN: i32 = 20;
pub const PANEL_GAP: i32 = 8;

pub const ICON_PX: f32 = 18.0;
pub const TEXT_PX: i32 = 17;
pub const LABEL_GAP: f32 = 10.0;
pub const CAPSULE_GAP: f32 = 10.0;
pub const ISLAND_PAD: f32 = 6.0;
pub const DIVIDER_HEIGHT_RATIO: f32 = 0.4;
pub const DIVIDER_WIDTH: f32 = 1.0;
pub const EXPAND_MS: f32 = 150.0;
pub const CLOSE_LINGER: Duration = Duration::from_millis(80);

pub const WORKSPACE_PILL_HEIGHT: f32 = 12.0;
pub const WORKSPACE_PILL_SPACING: f32 = 5.0;
pub const WORKSPACE_ACTIVE_SCALE: f32 = 2.0;
pub const WORKSPACE_ANIM_MS: f32 = 100.0;

pub const DOCK_ICON: f32 = ICON_PX;
pub const DOCK_SPACING: f32 = 10.0;
pub const DOCK_FOCUSED_ALPHA: f32 = 1.0;
pub const DOCK_UNFOCUSED_ALPHA: f32 = 0.5;
pub const DOCK_REORDER_MS: f32 = 200.0;

pub const PEEK_DURATION: Duration = Duration::from_millis(2000);
pub const PEEK_READY_DELAY: Duration = Duration::from_millis(1000);

// Pop! style
pub const POP_RADIUS: f32 = 15.0;
pub const POP_BADGE: f32 = 36.0;
pub const POP_BADGE_ACTIVE: f32 = 42.0;
pub const POP_BADGE_GAP: f32 = 6.0;
pub const POP_SIDE_MARGIN: i32 = 16;
pub const POP_ICON_GAP: f32 = 15.0;
pub const WORKSPACE_INFO_CAP: f32 = 260.0;
pub const WORKSPACE_INFO_NAME_PX: i32 = 13;
pub const WORKSPACE_INFO_TITLE_PX: i32 = 11;
/// How far the title tucks up into the name line's empty descent.
pub const WORKSPACE_INFO_LINE_TUCK: f32 = 3.0;

pub const BATTERY_CRITICAL_PERCENT: i32 = 10;

pub const OWNER_EXPAND: u64 = 100;
pub const OWNER_WORKSPACE: u64 = 200;
pub const OWNER_DOCK_WIDTH: u64 = 250;
pub const OWNER_DOCK: u64 = 1000;
