//! Overview grid, borders and timings.

// Grid
pub const COLUMNS: i32 = 5;
pub const ROWS: i32 = 1;
pub const PER_PAGE: i32 = COLUMNS * ROWS;
pub const SCALE: f32 = 0.15;
pub const GLOBAL_SCALE: f32 = 0.08;
pub const SPACING: f32 = 5.0;
pub const PADDING: f32 = 10.0;
pub const MARGIN: f32 = 10.0;
pub const GLOBAL_BLOCK_SPACING: f32 = 10.0;
// Rounding and borders
pub const BACKGROUND_BORDER_WIDTH: f32 = 2.0;
pub const WORKSPACE_BORDER_WIDTH: f32 = 2.0;
pub const WINDOW_BORDER_WIDTH: f32 = 2.0;
pub const INDICATOR_BORDER_WIDTH: f32 = 4.0;
// Workspace label
pub const NUMBER_PX: i32 = 40;
pub const NUMBER_FADE: f32 = 0.8;
// Window tile
pub const CORNER_ICON_RATIO: f32 = 0.2;
pub const CORNER_ICON_INSET: f32 = 4.0;
// Animation
pub const ANIM_MS: f32 = 200.0;
// Interaction
pub const FOCUS_GRACE_MS: i32 = 300;
pub const CAPTURE_INTERVAL_MS: i32 = 33;
