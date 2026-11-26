//! Bar panel card geometry, typography and polling periods.

// Card
pub const WIDTH: f32 = 400.0;
pub const MAX_HEIGHT: f32 = 520.0;
pub const PADDING: f32 = 20.0;
pub const HEADER_HEIGHT: f32 = 32.0;
pub const HEADER_DIVIDER_GAP: f32 = 4.0;
pub const HEADER_TAG_PAD: f32 = 8.0;
pub const CONTENT_GAP: f32 = 8.0;
pub const GAP_BELOW_BAR: f32 = 8.0;
pub const SIDE_MARGIN: f32 = 20.0;
pub const CLOSE_BUTTON: f32 = 22.0;
// Rows
pub const ROW_GAP: f32 = 6.0;
pub const LIST_SPACING: f32 = 5.0;
pub const TIGHT_GAP: f32 = 4.0;
pub const ROW_ICON_GAP: f32 = 8.0;
pub const DEVICE_ROW_HEIGHT: f32 = 50.0;
pub const TRAILING_SPACER: f32 = 4.0;
pub const EMPTY_HEIGHT: f32 = 72.0;
pub const ACTION_GAP: f32 = 4.0;
pub const ROW_TEXT_LEFT: f32 = 38.0;
pub const SECTION_HEIGHT: f32 = 22.0;
// Dialog
pub const DIALOG_SPACER: f32 = 14.0;
pub const DIALOG_LABEL: f32 = 20.0;
pub const DIALOG_TOP_MARGIN: f32 = 6.0;
pub const DIALOG_BUTTON: f32 = 28.0;
pub const DIALOG_BUTTON_PAD: f32 = 16.0;
pub const DIALOG_LABEL_CHARS: f32 = 32.0;
pub const CONFIRM_DIALOG_HEIGHT: f32 = DIALOG_SPACER + DIALOG_LABEL + ROW_GAP + DIALOG_BUTTON + ROW_GAP + 2.0 * PADDING;
pub const PAIRING_CODE_HEIGHT: f32 = 40.0;
pub const PAIRING_CODE_PX: i32 = 32;
pub const PAIRING_DIALOG_HEIGHT: f32 = DIALOG_SPACER + PAIRING_CODE_HEIGHT + ROW_GAP + DIALOG_LABEL + ROW_GAP + DIALOG_BUTTON + ROW_GAP + 2.0 * PADDING;
pub const FIELD_DIALOG_HEIGHT: f32 = DIALOG_SPACER + DIALOG_BUTTON + ROW_GAP + DIALOG_BUTTON + ROW_GAP + 2.0 * PADDING;
// Slider
pub const SLIDER_TRACK: f32 = 6.0;
pub const SLIDER_KNOB: f32 = 12.0;
// Animation
pub const REVEAL_MS: f32 = 220.0;
// Scroll
pub const SCROLL_ROLLER: f32 = 14.0;
pub const SCROLL_OVERHANG: f32 = 6.0;
pub const SCROLL_CAP: f32 = 6.0;
pub const SCROLL_BAND: f32 = 4.0;
// Typography
pub const TEXT_PX: i32 = 17;
pub const TITLE_PX: i32 = 17;
pub const ICON_PX: i32 = 18;
// Brightness
pub const BRIGHTNESS_ROW: f32 = 24.0;
pub const BRIGHTNESS_PCT_WIDTH: f32 = 40.0;
pub const BRIGHTNESS_ICON_GAP: f32 = 10.0;
pub const BRIGHTNESS_SLIDER_GAP: f32 = 8.0;
pub const BRIGHTNESS_WHEEL_STEP: i32 = 5;
// Battery
pub const BATTERY_EMPTY: f32 = 72.0;
pub const BATTERY_TEXT_ROW: f32 = 18.0;
pub const BATTERY_BAR_HEIGHT: f32 = 6.0;
pub const BATTERY_BAR_TOP_GAP: f32 = 12.0;
pub const BATTERY_ROW_BOTTOM_PAD: f32 = 14.0;
pub const BATTERY_ROW: f32 = BATTERY_TEXT_ROW + BATTERY_BAR_TOP_GAP + BATTERY_BAR_HEIGHT + BATTERY_ROW_BOTTOM_PAD;
// Media
pub const MEDIA_THUMB: f32 = 72.0;
pub const MEDIA_TITLE_LEFT: f32 = 12.0;
pub const MEDIA_TITLE_SPACING: f32 = 3.0;
pub const MEDIA_PROGRESS_ROW: f32 = 20.0;
pub const MEDIA_PROGRESS_TOP: f32 = 10.0;
pub const MEDIA_CONTROLS_ROW: f32 = 32.0;
pub const MEDIA_CONTROLS_TOP: f32 = 8.0;
pub const MEDIA_CONTROLS_SPACING: f32 = 8.0;
pub const MEDIA_SIDE_BUTTON: f32 = 28.0;
pub const MEDIA_PLAY_BUTTON: f32 = 32.0;
pub const MEDIA_POLL_MS: i32 = 1000;
// Time
pub const TIME_WIDTH: f32 = 280.0;
pub const TIME_BIG_ROW: f32 = 76.0;
pub const TIME_LINE: f32 = 26.0;
pub const TIME_ZONE_LINE: f32 = 18.0;
pub const TIME_GAP: f32 = 6.0;
pub const TIME_SECONDS_PX: i32 = 22;
// Date
pub const DATE_WIDTH: f32 = 504.0;
pub const DATE_COLUMN_GAP: f32 = 16.0;
pub const DATE_WEEKDAY_LINE: f32 = 26.0;
pub const DATE_LINE: f32 = 18.0;
pub const DATE_LINE_GAP: f32 = 2.0;
pub const DATE_BIG_DAY_ROW: f32 = 74.0;
pub const DATE_BIG_DAY_GAP: f32 = 6.0;
pub const DATE_WEEK_LINE: f32 = 16.0;
pub const DATE_GRID_HEADER: f32 = 24.0;
pub const DATE_GRID_HEADER_GAP: f32 = 15.0;
pub const DATE_WEEKDAY_ROW: f32 = 22.0;
pub const DATE_GRID_TOP_GAP: f32 = 2.0;
pub const DATE_CELL_PADDING: f32 = 4.0;
pub const DATE_NAV_BUTTON: f32 = 20.0;
pub const DATE_NAV_GAP: f32 = 6.0;
pub const DATE_TODAY_DOT: f32 = 6.0;
pub const DATE_BIG_DAY_PX: i32 = 64;
pub const DATE_WEEKDAY_PX: i32 = 22;
// Volume
pub const VOLUME_LABEL_ROW: f32 = 20.0;
pub const VOLUME_SLIDER_ROW: f32 = 24.0;
pub const VOLUME_SECTION_ROW: f32 = 24.0;
pub const VOLUME_TEXT_ROW: f32 = 18.0;
pub const VOLUME_APP_GAP: f32 = 4.0;
pub const VOLUME_APP_SLIDER: f32 = 16.0;
pub const VOLUME_APP_TRACK: f32 = 5.0;
pub const VOLUME_APP_PAD: f32 = 8.0;
pub const VOLUME_APP_ROW: f32 = VOLUME_TEXT_ROW + VOLUME_APP_GAP + VOLUME_APP_SLIDER + VOLUME_APP_PAD;
pub const VOLUME_DEVICE_ROW: f32 = VOLUME_TEXT_ROW + 6.0;
pub const VOLUME_PERCENT_WIDTH: f32 = 40.0;
pub const VOLUME_SLIDER_GAP: f32 = 8.0;
pub const VOLUME_LABEL_CAP: f32 = 180.0;
pub const VOLUME_BUTTON: f32 = 22.0;
pub const VOLUME_INDICATOR: f32 = 14.0;
pub const VOLUME_INDICATOR_DOT: f32 = 6.0;
// Tray
pub const TRAY_CELL: f32 = 40.0;
pub const TRAY_ICON: f32 = 20.0;
pub const TRAY_GAP: f32 = 5.0;
pub const TRAY_MENU_WIDTH: f32 = 220.0;
pub const TRAY_MENU_ROW: f32 = 28.0;
pub const TRAY_MENU_SEPARATOR: f32 = 8.0;
pub const TRAY_MENU_SEPARATOR_INSET: f32 = 6.0;
pub const TRAY_MENU_PAD: f32 = 4.0;
pub const TRAY_MENU_GAP: f32 = 5.0;
pub const TRAY_MENU_ROW_PAD: f32 = 8.0;
pub const TRAY_MENU_LABEL_OFFSET: f32 = 24.0;
pub const TRAY_EMPTY_HEIGHT: f32 = 40.0;
// Network
pub const NETWORK_BANNER: f32 = 48.0;
pub const NETWORK_BANNER_CLOSE: f32 = 18.0;
pub const TOGGLE_WIDTH: f32 = 36.0;
pub const TOGGLE_HEIGHT: f32 = 20.0;
pub const NETWORK_ETHERNET: f32 = 40.0;
pub const NETWORK_STATE: f32 = 72.0;
pub const NETWORK_SCANNING: f32 = 48.0;
pub const NETWORK_SECTION_FIRST: f32 = 20.0;
pub const NETWORK_SECTION: f32 = 24.0;
pub const NETWORK_FIELD_HEIGHT: f32 = 34.0;
pub const NETWORK_PASSWORD_MIN: i32 = 8;
pub const NETWORK_ERROR_CHARS: i32 = 48;
// Resource
pub const RESOURCE_MAX_HEIGHT: f32 = 680.0;
pub const RESOURCE_CARD_GAP: f32 = 10.0;
pub const CARD_TOP: f32 = 10.0;
pub const CARD_BOTTOM: f32 = 12.0;
pub const CARD_SIDE: f32 = 12.0;
pub const CARD_HEADER: f32 = 32.0;
pub const CARD_HEADER_GAP: f32 = 8.0;
pub const GAUGE_DIAMETER: f32 = 108.0;
pub const GAUGE_STROKE: f32 = 8.0;
pub const GAUGE_ROW_GAP: f32 = 16.0;
pub const GAUGE_LINE_GAP: f32 = 2.0;
pub const USAGE_WARN: f32 = 0.7;
pub const USAGE_CRITICAL: f32 = 0.9;
pub const TEMP_WARN: f32 = 70.0;
pub const TEMP_CRITICAL: f32 = 85.0;
pub const TEMP_GAUGE_MAX: f32 = 100.0;
pub const MEMORY_BAR: f32 = 6.0;
pub const MEMORY_MIN_FILL: f32 = 6.0;
pub const MEMORY_LABEL_GAP: f32 = 6.0;
pub const MEMORY_LINE_GAP: f32 = 12.0;
pub const RESOURCE_POLL_MS: i32 = 1000;
