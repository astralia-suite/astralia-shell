#pragma once

namespace astralia::overview_config {

// Grid
inline constexpr int columns = 5;
inline constexpr int rows = 1;
inline constexpr int per_page = columns * rows;
inline constexpr float scale = 0.15f;
inline constexpr float global_scale = 0.08f;
inline constexpr float spacing = 5.0f;
inline constexpr float padding = 10.0f;
inline constexpr float margin = 10.0f;
inline constexpr float global_block_spacing = 10.0f;

// Rounding and borders
inline constexpr float background_border_width = 2.0f;
inline constexpr float workspace_border_width = 2.0f;
inline constexpr float window_border_width = 2.0f;
inline constexpr float indicator_border_width = 4.0f;

// Workspace label
inline constexpr int number_px = 40;
inline constexpr float number_fade = 0.8f;

// Window tile
inline constexpr float corner_icon_ratio = 0.2f;
inline constexpr float corner_icon_inset = 4.0f;

// Animation
inline constexpr float anim_ms = 200.0f;

// Interaction
inline constexpr int focus_grace_ms = 300;
inline constexpr int capture_interval_ms = 33;

} // namespace astralia::overview_config
