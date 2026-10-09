#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <string_view>

namespace astralia::settings_config {

// Window
inline constexpr float card_margin = 40.0f;
inline constexpr float card_max_width = 920.0f;
inline constexpr float card_max_height = 680.0f;
inline constexpr float card_padding = 20.0f;
inline constexpr float header_height = 32.0f;
inline constexpr float header_divider_gap = 4.0f;
inline constexpr float content_gap = 8.0f;
inline constexpr float action_button_size = 22.0f;

// Rail
inline constexpr float rail_expanded_width = 200.0f;
inline constexpr float rail_collapsed_width = 64.0f;
inline constexpr float rail_collapse_breakpoint = 700.0f;
inline constexpr float rail_padding = 10.0f;
inline constexpr float rail_divider_gap = 16.0f;
inline constexpr float rail_item_height = 36.0f;
inline constexpr float rail_item_gap = 4.0f;
inline constexpr float rail_icon_label_gap = 10.0f;

// Profile
inline constexpr float avatar_size = 40.0f;
inline constexpr float avatar_border = 2.0f;
inline constexpr float profile_top_padding = 4.0f;
inline constexpr float profile_label_gap = 10.0f;
inline constexpr float profile_line_gap = 2.0f;
inline constexpr float profile_bottom_padding = 12.0f;
inline constexpr float profile_divider_gap = 12.0f;

// Rows and tiles
inline constexpr float row_gap = 6.0f;
inline constexpr float row_height = 40.0f;
inline constexpr float selector_height = 35.0f;
inline constexpr float selector_spacing = 6.0f;
inline constexpr float selector_border = 2.0f;
inline constexpr float tile_radius = 6.0f;
inline constexpr float tile_height = 48.0f;
inline constexpr float tile_border = 2.0f;
inline constexpr float tile_margin = 12.0f;
inline constexpr float tile_spacing = 10.0f;
inline constexpr float group_spacing = 8.0f;
inline constexpr float field_height = 28.0f;
inline constexpr float number_field_width = 72.0f;
inline constexpr float field_inset = 8.0f;
inline constexpr float reset_icon_size = 20.0f;

// Toggle
inline constexpr float toggle_width = 36.0f;
inline constexpr float toggle_height = 20.0f;
inline constexpr float toggle_knob = 14.0f;
inline constexpr float toggle_knob_inset = 3.0f;

// Wallpaper
inline constexpr float thumb_size = 115.0f;
inline constexpr float thumb_gap = 15.0f;
inline constexpr int thumb_columns = 5;
inline constexpr float thumb_radius = 8.0f;
inline constexpr float thumb_label_pad = 6.0f;
inline constexpr float grid_inset = 5.0f;
inline constexpr float scroll_speed = 3.0f;
inline constexpr float warning_pad = 10.0f;
inline constexpr float region_chip_height = 35.0f;
inline constexpr float region_chip_gap = 6.0f;
inline constexpr float stepper_button = 28.0f;
inline constexpr float dir_bar_height = 40.0f;
inline constexpr float dir_bar_label_margin = 14.0f;
inline constexpr float dir_bar_field_margin = 10.0f;
inline constexpr float dir_bar_edge_margin = 8.0f;
inline constexpr float dir_bar_button_width = 72.0f;
inline constexpr float dir_bar_button_height = 28.0f;
inline constexpr std::size_t max_images = 400;
inline constexpr std::size_t max_in_flight = 2;
inline constexpr int max_columns = 6;

// Idle
inline constexpr int idle_timeout_min = 10;
inline constexpr int idle_timeout_max = 1800;

// Animation
inline constexpr float tab_fade_ms = 90.0f;

// Typography
inline constexpr int text_px = 17;
inline constexpr int title_px = 20;
inline constexpr int small_px = 13;
inline constexpr int icon_px = 18;

// Opacity
inline constexpr float label_opacity = 0.85f;

// Focus
inline constexpr std::chrono::milliseconds relayout_grace{500};

} // namespace astralia::settings_config
