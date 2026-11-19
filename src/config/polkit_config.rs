//! Polkit card geometry, line heights and strings.

// Card geometry
pub const CARD_WIDTH: f64 = 480.0;
pub const CARD_PAD: f64 = 30.0;
pub const BORDER_WIDTH: f64 = 5.0;
pub const SPACING: f64 = 16.0;
pub const TITLE_TAG_PAD: f64 = 8.0;
pub const FIELD_HEIGHT: f64 = 55.0;
pub const DOT_MARGIN: f64 = 8.0;
pub const DOT_SIZE: i32 = 16;

// Line heights
pub const TITLE_LINE_HEIGHT: f64 = 20.0;
pub const MESSAGE_LINE_HEIGHT: f64 = 16.0;
pub const INFO_LINE_HEIGHT: f64 = 14.0;

// Content
pub const TITLE_TEXT: &str = "Authentication Required";
pub const PASSWORD_PLACEHOLDER: &str = "Password";
pub const AUTHENTICATING_TEXT: &str = "Authenticating...";
pub const ERROR_TEXT: &str = "Skill Issue";
pub const ECHO_FILE_PATH: &str = "polkit/electro.png";

// Layer namespace
pub const LAYER_NAMESPACE: &str = "astralia-shell-polkit";
