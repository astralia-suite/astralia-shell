//! Logout ring geometry, choreography and actions.

// Geometry
pub const BUTTON_COUNT: i32 = 8;
pub const BUTTON_SIZE: f64 = 110.0;
pub const RING_RADIUS: f64 = 300.0;
pub const HIGHLIGHT_SCALE: f64 = 1.05;
pub const LOGO_SIZE: i32 = 250;
// Typography
pub const GLYPH_PX: i32 = (BUTTON_SIZE / 2.0) as i32;
// Choreography
pub const LOGO_ANIM_MS: f32 = 600.0;
pub const BUTTON_SCALE_MS: f32 = 140.0;
pub const BUTTON_BORDER_MS: f32 = 160.0;
pub const HOLD_MS: f32 = 300.0;
pub const SLASH_MS: f32 = 120.0;
pub const SLASH_ADVANCE_FRAC: f32 = 0.75;
pub const BURST_MS: f32 = 500.0;
pub const PUSH_MS: f32 = 260.0;
pub const EXIT_SPREAD: f32 = 0.55;
pub const EXIT_FADE_MS: f32 = BURST_MS * 0.8;
pub const CHARGE_MS: f32 = 300.0;
pub const SCRIM_PEAK: f32 = 0.35;
pub const SCRIM_OUT_FRAC: f32 = 0.6;
pub const FLASH_PEAK: f32 = 0.6;
pub const FLASH_FRAC: f32 = 0.28;
pub const STAR_STEP: i32 = 3;
pub const LOCK_INDEX: i32 = 2;
/// `astralia` plus the runtime suffix, so the button locks through this shell.
pub const LOCK_COMMAND: &str = "astralia lock";
pub const LOGO_ASSET: &str = "logout/logo.png";
// Ring angles
pub const START_ANGLE: f64 = -std::f64::consts::PI / 2.0;
pub const STEP_ANGLE: f64 = 2.0 * std::f64::consts::PI / BUTTON_COUNT as f64;
// Actions

#[derive(Clone, Copy, Debug)]
pub struct LogoutAction {
    pub glyph: &'static str,
    pub command: &'static str,
}

pub const ACTIONS: [LogoutAction; BUTTON_COUNT as usize] = [
    LogoutAction { glyph: "劍", command: "systemctl poweroff" },
    LogoutAction { glyph: "光", command: "systemctl reboot" },
    LogoutAction { glyph: "如", command: "" },
    LogoutAction { glyph: "我", command: "systemctl reboot --firmware-setup" },
    LogoutAction { glyph: "斬", command: "" },
    LogoutAction { glyph: "盡", command: "" },
    LogoutAction { glyph: "蕪", command: "" },
    LogoutAction { glyph: "雜", command: "" },
];
