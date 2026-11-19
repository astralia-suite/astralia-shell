//! Bamboo window size, timing, grid and palette.

// Window size
pub const DEFAULT_WINDOW_WIDTH: i32 = 560;
pub const DEFAULT_WINDOW_HEIGHT: i32 = 600;
// Timing
pub const TICK_INTERVAL_MS: f32 = 80.0;
pub const HOLD_TICKS: i32 = 60;
pub const FADE_STEP: f32 = 0.1;
// Character grid
pub const CELL_WIDTH_PX: f32 = 10.0;
pub const CELL_HEIGHT_PX: f32 = 20.0;
pub const FONT_PX: f32 = 18.0;
// Ground and shoots
pub const GROUND_MARGIN_ROWS: i32 = 1;
pub const GROUND_INSET_COLS: i32 = 2;
pub const SHOOT_COUNT: i32 = 14;
pub const SHOOT_MAX_ROWS: i32 = 2;
// Culms
pub const CULM_COUNT: i32 = 8;
pub const CULM_MIN_HEIGHT_RATIO: f32 = 0.5;
pub const CULM_MAX_HEIGHT_RATIO: f32 = 0.92;
pub const CULM_GLYPH: char = '│';
pub const NODE_SPACING_MIN_ROWS: i32 = 2;
pub const NODE_SPACING_MAX_ROWS: i32 = 3;
// Branches and leaves
pub const BRANCH_ZONE_RATIO: f32 = 0.4;
pub const BRANCH_CHANCE: f32 = 0.75;
pub const BRANCH_MIN_LENGTH: i32 = 4;
pub const BRANCH_MAX_LENGTH: i32 = 8;
pub const BRANCH_FLAT_CHANCE: f32 = 0.6;
pub const LEAF_SPACING: i32 = 1;
pub const LEAF_GLYPH_RIGHT: char = '>';
pub const LEAF_GLYPH_LEFT: char = '<';
// Colours as 0xAARRGGBB
pub const GROUND_COLOR: u32 = 0xFFA0_8A6E;
pub const SHOOT_COLOR: u32 = 0xFFC8_D88A;
pub const CULM_COLOR: u32 = 0xFF6F_A84F;
pub const NODE_COLOR: u32 = 0xFF3F_8A52;
pub const BRANCH_COLOR: u32 = 0xFF5E_9B4A;
pub const LEAF_COLOR: u32 = 0xFF8F_D16A;
