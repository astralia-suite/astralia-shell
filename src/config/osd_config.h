#pragma once

#include <chrono>

namespace astralia::osd_config {

// Surface
inline constexpr int width = 300;
inline constexpr int height = 50;
inline constexpr int margin_bottom = 30;

// Content
inline constexpr int content_margin = height / 2;
inline constexpr int bar_margin = 10;
inline constexpr int label_width = 44;

// Typography
inline constexpr int icon_px = 18;
inline constexpr int label_px = 20;
inline constexpr float track_height = 6.0f;
inline constexpr float border_width = 2.0f;

// Timing
inline constexpr float anim_normal_ms = 220.0f;
inline constexpr float anim_fast_ms = 150.0f;
inline constexpr std::chrono::milliseconds visible_for{2000};
inline constexpr std::chrono::milliseconds ready_delay{1000};

} // namespace astralia::osd_config
