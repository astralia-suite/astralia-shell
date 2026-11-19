//! Rain window size, timing and sim geometry.

// Window size
pub const DEFAULT_WINDOW_WIDTH: i32 = 480;
pub const DEFAULT_WINDOW_HEIGHT: i32 = 600;
// Shared rain timing
pub const FALL_INTERVAL_MS: f32 = 45.0;
pub const RESET_CHANCE: f32 = 0.025;
pub const FADE_ALPHA: f32 = 0.05;
// Async fall speed
pub const ASYNC_SPEED_MIN: f32 = 0.4;
pub const ASYNC_SPEED_MAX: f32 = 1.0;
// Matrix sim: font and cell geometry
pub const MATRIX_FONT_PX: f32 = 20.0;
pub const MATRIX_CELL_WIDTH: f32 = 12.0;
pub const MATRIX_CELL_HEIGHT: f32 = 24.0;
pub const MATRIX_BOLD_CHANCE: f32 = 0.5;
// Stiletto sim: comet row geometry
pub const STILETTO_COLUMN_SPACING_PX: f32 = 24.0;
pub const STILETTO_HEAD_HEIGHT_PX: f32 = 24.0;
pub const STILETTO_STEP_PX: f32 = 24.0;
pub const STILETTO_TRAIL_WIDTH_PX: f32 = 3.0;
