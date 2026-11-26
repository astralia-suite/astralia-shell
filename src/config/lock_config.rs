//! Lock screen geometry, typography, transition timeline and strings.

// Development unlock corner
pub const DEV_UNLOCK_WIDTH: f32 = 140.0;
pub const DEV_UNLOCK_HEIGHT: f32 = 48.0;

// Background & dot
pub const BG_BORDER_WIDTH: f32 = 5.0;

pub const DOT_SIZE: f32 = 18.0;

// Fonts
pub const FONT_ICON: f32 = 200.0;
pub const FONT_CLOCK: f32 = 120.0;
pub const FONT_DATE: f32 = 34.0;
pub const FONT_NORMAL: f32 = 20.0;
pub const FONT_MONO: f32 = 18.0;

// Card layout
pub const CARD_HEIGHT_MULT: f32 = 0.7;
pub const CARD_RATIO: f32 = 16.0 / 9.0;
pub const PANEL_GAP: f32 = 25.0;
pub const CENTER_WIDTH: f32 = 560.0;
pub const CENTER_REF_HEIGHT: f32 = 1440.0;

// Icon box
pub const ICON_BOX_MARGIN: f32 = 20.0;

// Clock/date/avatar/input gaps
pub const CLOCK_GAP: f32 = 10.0;
pub const GAP_CLOCK_DATE: f32 = 6.0;
pub const GAP_DATE_AVATAR: f32 = 40.0;
pub const GAP_AVATAR_INPUT: f32 = 34.0;
pub const GAP_INPUT_MESSAGE: f32 = 18.0;

// Input & pill
pub const INPUT_HEIGHT: f32 = 56.0;
pub const INPUT_WIDTH_FRAC: f32 = 0.85;
pub const PILL_PAD: f32 = 12.0;
pub const PILL_ICON_SIZE: f32 = 26.0;
pub const PILL_BUTTON_SIZE: f32 = 44.0;

// Profile
pub const PROFILE_BORDER_WIDTH: f32 = 4.0;
pub const PROFILE_SIZE: f32 = 180.0;

// Side panel
pub const SIDE_PANEL_PAD: f32 = 20.0;

// Fetch
pub const FETCH_LINE_GAP: f32 = 6.0;
pub const FETCH_CHIP_PAD: f32 = 8.0;
pub const FETCH_COLOR_BOX: f32 = 26.0;
pub const FETCH_COLOR_GAP: f32 = 10.0;

// Card chrome
pub const CARD_HEADER_GAP: f32 = 12.0;
pub const CARD_TAG_PAD: f32 = 6.0;
pub const BATTERY_ROW_GAP: f32 = 8.0;
pub const BATTERY_ICON_GAP: f32 = 10.0;
pub const BATTERY_BAR_HEIGHT: f32 = 6.0;
pub const BATTERY_BAR_RADIUS: f32 = 3.0;

// Media
pub const MEDIA_ART: f32 = 116.0;
pub const MEDIA_BTN_SIZE: f32 = 40.0;
pub const MEDIA_BTN_GAP: f32 = 12.0;
pub const MEDIA_TEXT_GAP: f32 = 4.0;

// Resource gauges
pub const RES_TILE_GAP: f32 = 14.0;
pub const RES_ICON_SIZE: f32 = 26.0;
pub const RES_VALUE_FONT: f32 = 32.0;
pub const RES_TEMP_WARN_C: f32 = 85.0;
pub const RES_GAUGE_STROKE_RATIO: f32 = 6.0 / 68.0;
pub const RES_GAUGE_ICON_VALUE_GAP: f32 = 2.0;
pub const RES_GAUGE_LABEL_GAP: f32 = 6.0;
pub const RES_GAUGE_GPU_COLOR_HEX: &str = "#3FB6C8";

// Notifications
pub const NOTIF_CARD_GAP: f32 = 8.0;
pub const NOTIF_CARD_PAD: f32 = 12.0;
pub const NOTIF_MAX_CARDS: i32 = 6;

// Scale state
pub const SCALE_FULL: f32 = 1.0;

// Transition timeline in milliseconds
pub const SLASH_MS: f32 = 2800.0;
pub const STAR_COUNT: usize = 5;
pub const FLURRY_COUNT: usize = 14;
pub const SLASH_COUNT: usize = STAR_COUNT + FLURRY_COUNT;
pub const SLASH_FIRST_START_MS: f32 = 40.0;
pub const SLASH_STRIKE_MS: f32 = 260.0;
pub const STAR_ADVANCE_MS: f32 = 100.0;
pub const SLASH_WAIT_MS: f32 = 600.0;
pub const SLASH_FINAL_DRAW_MS: f32 = 70.0;
pub const SLASH_FINAL_FADE_MS: f32 = 280.0;
pub const SLASH_IMPACT_MS: f32 = 60.0;
pub const SLASH_SHOCK_MS: f32 = 200.0;
pub const SLASH_FLASH_HOLD_MS: f32 = 60.0;
pub const SLASH_FLASH_MS: f32 = 400.0;
pub const SLASH_FINAL_BEAM_MS: f32 = 280.0;
pub const SLASH_SLIVER_MS: f32 = 420.0;
pub const SLASH_DIM_RAMP_MS: f32 = 300.0;
pub const SLASH_DIM_LIFT_MS: f32 = 320.0;
pub const SLASH_SHATTER_HOLD_MS: f32 = 25.0;
pub const SLASH_SHATTER_SPREAD_MS: f32 = 200.0;
pub const SLASH_FALL_MS: f32 = 320.0;

// Transition star
#[derive(Clone, Copy, Debug)]
pub struct StarCorner {
    pub x: f32,
    pub y: f32,
}

pub const STAR_CORNERS: [StarCorner; STAR_COUNT] = [
    StarCorner { x: 0.50, y: 0.24 },
    StarCorner { x: 0.78, y: 0.77 },
    StarCorner { x: 0.24, y: 0.36 },
    StarCorner { x: 0.80, y: 0.36 },
    StarCorner { x: 0.16, y: 0.79 },
];

// Transition flurry
#[derive(Clone, Copy, Debug)]
pub struct FlurrySlash {
    pub degrees: f32,
    pub offset: f32,
    pub start_ms: f32,
}

pub const REFERENCE_WIDTH: f32 = 16.0;
pub const REFERENCE_HEIGHT: f32 = 9.0;
pub const FLURRY_SLASHES: [FlurrySlash; FLURRY_COUNT] = [
    FlurrySlash { degrees: -89.0, offset: 0.28, start_ms: 480.0 },
    FlurrySlash { degrees: 34.0, offset: 0.16, start_ms: 560.0 },
    FlurrySlash { degrees: -10.0, offset: -0.22, start_ms: 720.0 },
    FlurrySlash { degrees: -32.0, offset: 0.16, start_ms: 800.0 },
    FlurrySlash { degrees: -63.0, offset: -0.29, start_ms: 830.0 },
    FlurrySlash { degrees: 11.0, offset: 0.26, start_ms: 880.0 },
    FlurrySlash { degrees: 77.0, offset: -0.30, start_ms: 910.0 },
    FlurrySlash { degrees: 53.0, offset: 0.34, start_ms: 960.0 },
    FlurrySlash { degrees: -79.0, offset: -0.34, start_ms: 1040.0 },
    FlurrySlash { degrees: 69.0, offset: 0.30, start_ms: 1070.0 },
    FlurrySlash { degrees: -71.0, offset: -0.30, start_ms: 1120.0 },
    FlurrySlash { degrees: -55.0, offset: 0.23, start_ms: 1200.0 },
    FlurrySlash { degrees: 85.0, offset: -0.29, start_ms: 1220.0 },
    FlurrySlash { degrees: 61.0, offset: 0.25, start_ms: 1240.0 },
];

// Transition timeline as shares of progress
pub const SLASH_FIRST_START: f32 = SLASH_FIRST_START_MS / SLASH_MS;
pub const SLASH_DRAW_SPAN: f32 = SLASH_STRIKE_MS / SLASH_MS;
pub const SLASH_FINAL_START: f32 = (SLASH_FIRST_START_MS + FLURRY_SLASHES[FLURRY_COUNT - 1].start_ms + SLASH_STRIKE_MS + SLASH_WAIT_MS) / SLASH_MS;
pub const SLASH_FINAL_DRAW_SPAN: f32 = SLASH_FINAL_DRAW_MS / SLASH_MS;
pub const SLASH_FINAL_FADE: f32 = SLASH_FINAL_FADE_MS / SLASH_MS;
pub const SLASH_IMPACT_SPAN: f32 = SLASH_IMPACT_MS / SLASH_MS;
pub const SLASH_SHOCK_SPAN: f32 = SLASH_SHOCK_MS / SLASH_MS;
pub const SLASH_FLASH_HOLD: f32 = SLASH_FLASH_HOLD_MS / SLASH_MS;
pub const SLASH_FLASH_SPAN: f32 = SLASH_FLASH_MS / SLASH_MS;
pub const SLASH_FINAL_BEAM_SPAN: f32 = SLASH_FINAL_BEAM_MS / SLASH_MS;
pub const SLASH_SLIVER_SPAN: f32 = SLASH_SLIVER_MS / SLASH_MS;
pub const SLASH_DIM_RAMP: f32 = SLASH_DIM_RAMP_MS / SLASH_MS;
pub const SLASH_DIM_LIFT: f32 = SLASH_DIM_LIFT_MS / SLASH_MS;
pub const SLASH_SHATTER_HOLD: f32 = SLASH_SHATTER_HOLD_MS / SLASH_MS;
pub const SLASH_SHATTER_SPREAD: f32 = SLASH_SHATTER_SPREAD_MS / SLASH_MS;
pub const SLASH_FALL_SPAN: f32 = SLASH_FALL_MS / SLASH_MS;

// Transition geometry
pub const SLASH_GAP_FRAC: f32 = 0.012;
pub const SLASH_MIN_PIECE_AREA: f32 = 1.0;
pub const SLASH_FINAL_REACH: f32 = 0.05;
pub const FINAL_TILT: f32 = 0.14;
pub const SLIVER_COUNT: usize = 36;
pub const SLIVER_LENGTH_MIN: f32 = 24.0;
pub const SLIVER_LENGTH_MAX: f32 = 80.0;
pub const SLIVER_THICK_MIN: f32 = 2.0;
pub const SLIVER_THICK_MAX: f32 = 4.0;
pub const SLIVER_SPREAD_FRAC: f32 = 0.14;
pub const SLIVER_DRIFT_FRAC: f32 = 0.10;
pub const SLIVER_ALONG_FRAC: f32 = 0.12;
pub const SLASH_STRIKE_OVERSHOOT: f32 = 0.05;
pub const SLASH_SHARD_DRIFT: f32 = 0.06;
pub const SLASH_SHARD_SPIN: f32 = 0.25;
pub const SLASH_SHARD_RANK_NOISE: f32 = 0.25;
pub const SLASH_RAY_COUNT: usize = 18;
pub const SLASH_RAY_HALF_ANGLE: f32 = 0.012;

// Transition thunder
pub const STRIKE_AMP: f32 = 13.0;
pub const STRIKE_PEAK: f32 = 1.2;
pub const FINAL_THICK: f32 = 3.4;
pub const FINAL_AMP_MULT: f32 = 2.4;
pub const FINAL_INTENSITY: f32 = 2.8;
pub const FINAL_SEED: f32 = 21.0;
pub const SHOCK_RADIUS_FRAC: f32 = 0.6;
pub const SHOCK_INTENSITY: f32 = 1.3;

// Transition paint
pub const CUTMARK_WIDTH: f32 = 4.0;
pub const CUTMARK_ALPHA: f32 = 0.85;
pub const FALLBACK_BOLT_WIDTH: f32 = 3.0;
pub const BOLT_GLOW_NEAR_SCALE: f32 = 2.8;
pub const BOLT_GLOW_FAR_SCALE: f32 = 6.0;
pub const BOLT_GLOW_NEAR_ALPHA: f32 = 0.35;
pub const BOLT_GLOW_FAR_ALPHA: f32 = 0.15;
pub const SLASH_RAY_ALPHA: f32 = 0.5;
pub const SLASH_FLASH_PEAK: f32 = 0.9;
pub const DIM_PEAK: f32 = 0.5;
pub const DIM_HEX: &str = "#0B0620";
pub const FLASH_HEX: &str = "#EDE6FF";
pub const BEAM_PEAK_FRAC: f32 = 0.05;
pub const FINAL_BEAM_FRAC: f32 = 0.3;
pub const FINAL_THIN_FRAC: f32 = 0.012;
pub const BEAM_GLOW_SCALE: f32 = 1.8;
pub const BEAM_GLOW_ALPHA: f32 = 0.3;
pub const SLIVER_ALPHA: f32 = 0.9;

// Fail timer
pub const TIMER_FAIL_MS: f32 = 3000.0;

// Text strings
pub const FAIL_TEXT: &str = "Skill Issue";
pub const PLACEHOLDER_TEXT: &str = "Enter your password";
pub const LOADING_TEXT: &str = "Loading...";

// Assets
pub const ECHO_ASSET: &str = "polkit/electro.png";

// Animation owners
pub const OWNER_DOT_ROW_X: u64 = 8;
pub const OWNER_DOT_BASE: u64 = 1000;
