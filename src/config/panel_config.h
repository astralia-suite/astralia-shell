#pragma once

namespace astralia::panel_config {

// Card
inline constexpr float width = 400.0f;
inline constexpr float max_height = 520.0f;
inline constexpr float padding = 20.0f;
inline constexpr float header_height = 32.0f;
inline constexpr float header_divider_gap = 4.0f;
inline constexpr float content_gap = 8.0f;
inline constexpr float gap_below_bar = 8.0f;
inline constexpr float side_margin = 20.0f;
inline constexpr float close_button = 22.0f;

// Rows
inline constexpr float row_gap = 6.0f;
inline constexpr float list_spacing = 5.0f;
inline constexpr float tight_gap = 4.0f;
inline constexpr float row_icon_gap = 8.0f;
inline constexpr float device_row_height = 50.0f;
inline constexpr float trailing_spacer = 4.0f;
inline constexpr float empty_height = 72.0f;
inline constexpr float section_height = 22.0f;

// Slider
inline constexpr float slider_track = 6.0f;
inline constexpr float slider_knob = 12.0f;

// Animation
inline constexpr float reveal_ms = 220.0f;

// Typography
inline constexpr int text_px = 17;
inline constexpr int small_px = 13;
inline constexpr int title_px = 17;
inline constexpr int icon_px = 18;

// Brightness
inline constexpr float brightness_row = 24.0f;
inline constexpr float brightness_pct_width = 40.0f;
inline constexpr float brightness_icon_gap = 10.0f;
inline constexpr float brightness_slider_gap = 8.0f;
inline constexpr int brightness_wheel_step = 5;

// Battery
inline constexpr float battery_empty = 72.0f;
inline constexpr float battery_text_row = 18.0f;
inline constexpr float battery_bar_height = 6.0f;
inline constexpr float battery_bar_top_gap = 12.0f;
inline constexpr float battery_row_bottom_pad = 14.0f;
inline constexpr float battery_row = battery_text_row + battery_bar_top_gap + battery_bar_height + battery_row_bottom_pad;

// Media
inline constexpr float media_thumb = 72.0f;
inline constexpr float media_thumb_radius = 8.0f;
inline constexpr float media_title_left = 12.0f;
inline constexpr float media_title_spacing = 3.0f;
inline constexpr float media_progress_row = 20.0f;
inline constexpr float media_progress_top = 10.0f;
inline constexpr float media_controls_row = 32.0f;
inline constexpr float media_controls_top = 8.0f;
inline constexpr float media_controls_spacing = 8.0f;
inline constexpr float media_side_button = 28.0f;
inline constexpr float media_play_button = 32.0f;
inline constexpr int media_poll_ms = 1000;

// Clock
inline constexpr float clock_width = 504.0f;
inline constexpr float clock_column_gap = 16.0f;
inline constexpr float clock_weekday_line = 26.0f;
inline constexpr float clock_date_line = 18.0f;
inline constexpr float clock_line_gap = 2.0f;
inline constexpr float clock_big_day_row = 74.0f;
inline constexpr float clock_big_day_gap = 6.0f;
inline constexpr float clock_week_line = 16.0f;
inline constexpr float clock_grid_header = 24.0f;
inline constexpr float clock_grid_header_gap = 15.0f;
inline constexpr float clock_weekday_row = 22.0f;
inline constexpr float clock_grid_top_gap = 2.0f;
inline constexpr float clock_cell_padding = 4.0f;
inline constexpr float clock_nav_button = 20.0f;
inline constexpr float clock_nav_gap = 6.0f;
inline constexpr float clock_today_dot = 6.0f;
inline constexpr int clock_big_day_px = 64;
inline constexpr int clock_weekday_px = 22;

// Volume
inline constexpr float volume_label_row = 20.0f;
inline constexpr float volume_slider_row = 24.0f;
inline constexpr float volume_section_row = 24.0f;
inline constexpr float volume_device_row = 28.0f;
inline constexpr float volume_percent_width = 52.0f;
inline constexpr float volume_button = 22.0f;
inline constexpr float volume_indicator = 14.0f;
inline constexpr int volume_wheel_step = 5;

// Tray
inline constexpr float tray_cell = 40.0f;
inline constexpr float tray_icon = 20.0f;
inline constexpr float tray_gap = 4.0f;
inline constexpr float tray_menu_row = 28.0f;
inline constexpr float tray_menu_separator = 8.0f;
inline constexpr float tray_menu_pad = 8.0f;
inline constexpr float tray_menu_max = 360.0f;
inline constexpr float tray_menu_row_pad = 8.0f;
inline constexpr int tray_menu_wheel_step = 28;

// Network
inline constexpr float network_banner = 48.0f;
inline constexpr float network_ethernet = 40.0f;
inline constexpr float network_state = 72.0f;
inline constexpr float network_scanning = 48.0f;
inline constexpr float network_section_first = 20.0f;
inline constexpr float network_section = 24.0f;
inline constexpr float network_field_height = 34.0f;
inline constexpr float network_action_button = 22.0f;
inline constexpr int network_password_min = 8;
inline constexpr int network_error_chars = 48;

// Resource
inline constexpr float resource_max_height = 680.0f;
inline constexpr float resource_card_gap = 10.0f;
inline constexpr float card_top = 10.0f;
inline constexpr float card_bottom = 12.0f;
inline constexpr float card_side = 12.0f;
inline constexpr float card_header = 32.0f;
inline constexpr float card_header_gap = 8.0f;
inline constexpr float card_radius = 12.0f;
inline constexpr float gauge_diameter = 108.0f;
inline constexpr float gauge_stroke = 8.0f;
inline constexpr float gauge_row_gap = 16.0f;
inline constexpr float gauge_line_gap = 2.0f;
inline constexpr float usage_warn = 0.7f;
inline constexpr float usage_critical = 0.9f;
inline constexpr float temp_warn = 70.0f;
inline constexpr float temp_critical = 85.0f;
inline constexpr float temp_gauge_max = 100.0f;
inline constexpr float memory_bar = 6.0f;
inline constexpr float memory_min_fill = 6.0f;
inline constexpr float memory_label_gap = 6.0f;
inline constexpr float memory_line_gap = 12.0f;
inline constexpr int resource_poll_ms = 1000;

} // namespace astralia::panel_config
