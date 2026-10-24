#pragma once

namespace astralia::rain_config {

// window size
constexpr int default_window_width = 480;
constexpr int default_window_height = 600;

// shared rain timing
constexpr float fall_interval_ms = 45.0f;
constexpr float reset_chance = 0.025f;
constexpr float fade_alpha = 0.05f;

// async fall speed
constexpr float async_speed_min = 0.4f;
constexpr float async_speed_max = 1.0f;

// matrix sim: font and cell geometry
constexpr float matrix_font_px = 20.0f;
constexpr float matrix_cell_width = 12.0f;
constexpr float matrix_cell_height = 24.0f;
constexpr float matrix_bold_chance = 0.5f;

// stiletto sim: comet row geometry
constexpr float stiletto_column_spacing_px = 24.0f;
constexpr float stiletto_head_height_px = 24.0f;
constexpr float stiletto_step_px = 24.0f;
constexpr float stiletto_trail_width_px = 3.0f;

} // namespace astralia::rain_config

namespace astralia {

enum class RainMode { Matrix, Stiletto };

struct RainParams {
    RainMode mode = RainMode::Matrix;
    bool async_speed = false;

    bool operator==(const RainParams &) const = default;
};

} // namespace astralia
