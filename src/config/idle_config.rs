//! Idle activity pulse, screensaver logo and overlay fades.

// Activity pulse
pub const RECENT_ACTIVITY_PULSE_SECONDS: u32 = 2;
// Screensaver logo
pub const LOGO_SPEED: f32 = 90.0;
pub const LOGO_SIZE: f32 = 200.0;
pub const LOGO_ASSET: &str = "stellar-restoration.svg";
// Overlay fade
pub const IDLE_FADE_MS: f32 = 400.0;
pub const AMBIENT_FADE_OWNER: u64 = 1;
pub const SCREENSAVER_FADE_OWNER: u64 = 2;
// Layer-shell namespace, before the runtime suffix
pub const IDLE_LAYER_NAMESPACE: &str = "astralia-shell-idle-overlay";
