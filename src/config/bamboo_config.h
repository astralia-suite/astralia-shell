#pragma once

#include <cstdint>

namespace astralia::bamboo_config {

// window size
constexpr int default_window_width = 560;
constexpr int default_window_height = 600;

// timing
constexpr float tick_interval_ms = 80.0f;
constexpr int hold_ticks = 60;
constexpr float fade_step = 0.1f;

// character grid
constexpr float cell_width_px = 10.0f;
constexpr float cell_height_px = 20.0f;
constexpr float font_px = 18.0f;

// ground and shoots
constexpr int ground_margin_rows = 1;
constexpr int ground_inset_cols = 2;
constexpr int shoot_count = 14;
constexpr int shoot_max_rows = 2;

// culms
constexpr int culm_count = 8;
constexpr float culm_min_height_ratio = 0.5f;
constexpr float culm_max_height_ratio = 0.92f;
constexpr char32_t culm_glyph = U'│';
constexpr int node_spacing_min_rows = 2;
constexpr int node_spacing_max_rows = 3;

// branches and leaves
constexpr float branch_zone_ratio = 0.4f;
constexpr float branch_chance = 0.75f;
constexpr int branch_min_length = 4;
constexpr int branch_max_length = 8;
constexpr float branch_flat_chance = 0.6f;
constexpr int leaf_spacing = 1;
constexpr char32_t leaf_glyph_right = U'>';
constexpr char32_t leaf_glyph_left = U'<';

// colors as 0xAARRGGBB
constexpr uint32_t ground_color = 0xFFA08A6E;
constexpr uint32_t shoot_color = 0xFFC8D88A;
constexpr uint32_t culm_color = 0xFF6FA84F;
constexpr uint32_t node_color = 0xFF3F8A52;
constexpr uint32_t branch_color = 0xFF5E9B4A;
constexpr uint32_t leaf_color = 0xFF8FD16A;

} // namespace astralia::bamboo_config
