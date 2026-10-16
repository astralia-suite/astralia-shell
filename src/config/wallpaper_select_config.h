#pragma once

#include <cstddef>

namespace astralia::wallpaper_select_config {

// Cards
inline constexpr float card_scale = 0.44f;
inline constexpr float neighbour_scale = 0.56f;
inline constexpr float card_gap = 24.0f;
inline constexpr float neighbour_fade = 0.3f;
inline constexpr int visible_neighbours = 3;

// Images
inline constexpr int centre_px = 1920;
inline constexpr int neighbour_px = 640;
inline constexpr std::size_t max_images = 400;

// Label
inline constexpr float label_gap = 22.0f;
inline constexpr float label_padding = 12.0f;
inline constexpr float label_height = 34.0f;
inline constexpr int label_px = 15;

// Motion
inline constexpr float zoom_ms = 360.0f;
inline constexpr float step_ms = 240.0f;
inline constexpr float fade_in_ms = 160.0f;
inline constexpr float reveal_timeout_ms = 250.0f;
inline constexpr float handoff_timeout_ms = 1500.0f;
inline constexpr float handoff_settle_ms = 50.0f;
inline constexpr float wheel_notch = 1.0f;
inline constexpr float wayland_wheel_unit = 10.0f;

} // namespace astralia::wallpaper_select_config
