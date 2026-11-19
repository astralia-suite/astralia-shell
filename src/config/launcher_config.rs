//! Launcher commands, search limits, geometry and timings.

// Launch commands
pub const BROWSER: &str = "astralia-open browser";
pub const EDITOR: &str = "astralia-open editor";
pub const FILE_MANAGER: &str = "astralia-open file-manager";
pub const TERMINAL: &str = "astralia-open terminal";
pub const OPEN: &str = "xdg-open";
// Web search
pub const GOOGLE_URL: &str = "https://www.google.com/search?q=";
pub const DUCKDUCKGO_URL: &str = "https://duckduckgo.com/?q=";
pub const YOUTUBE_URL: &str = "https://www.youtube.com/results?search_query=";
// Search
pub const DEBOUNCE_MS: i32 = 30;
pub const DOUBLE_CLICK_MS: i32 = 400;
pub const MAX_RESULTS: i32 = 20;
pub const LISTING_MAX_RESULTS: i32 = 50;
pub const INDEX_REFRESH_S: i32 = 60;
// Visit store
pub const VISITS_FILE: &str = "astralia-shell/launcher_visits";
// Geometry
pub const WIDTH: f32 = 700.0;
pub const PAD: f32 = 10.0;
pub const MENU_PAD: f32 = 14.0;
pub const BORDER_WIDTH: f32 = 2.0;
pub const MENU_BORDER_WIDTH: f32 = 4.0;
pub const HIGHLIGHT_BORDER_WIDTH: f32 = 2.0;
pub const BULLET_SIZE: f32 = 25.0;
pub const BULLET_GAP: f32 = PAD;
pub const SEARCH_HEIGHT: f32 = 40.0;
pub const ROW_HEIGHT: f32 = 44.0;
pub const ROW_SPACING: f32 = 10.0;
pub const ROW_PITCH: f32 = ROW_HEIGHT + ROW_SPACING;
pub const LIST_TOP: f32 = 64.0;
pub const LIST_GAP: f32 = 10.0;
pub const CARET_WIDTH: f32 = 2.0;
pub const TWO_LINE_GAP: f32 = -3.0;
pub const CLIP_INSET: f32 = 10.0;
pub const ICON_SIZE: i32 = 18;
pub const MAX_VISIBLE: i32 = 6;
pub const MAX_ROW_CHARS: usize = 74;
// Typography
pub const LABEL_PX: i32 = 17;
pub const SMALL_PX: i32 = 12;
// Animation
pub const HEIGHT_ANIM_MS: f32 = 200.0;
pub const HIGHLIGHT_ANIM_MS: f32 = 140.0;
pub const ENTRANCE_MS: f32 = 790.0;
pub const BLAST_REACH: f32 = 200.0;
// Bullets
pub const BULLET_ASSET_PREFIX: &str = "constellation/C";
pub const BULLET_ASSET_SUFFIX: &str = ".png";
