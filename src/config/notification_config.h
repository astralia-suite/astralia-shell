#pragma once

#include <chrono>

namespace astralia::notification_config {

// Placement
inline constexpr int margin_right = 10;
inline constexpr int margin_bottom = 10;
inline constexpr float gap = 8.0f;
inline constexpr float max_stack_height = 480.0f;

// Card geometry
inline constexpr float card_width = 400.0f;
inline constexpr float card_pad = 16.0f;
inline constexpr float card_radius = 10.0f;
inline constexpr float border_width = 2.0f;
inline constexpr float content_spacing = 10.0f;
inline constexpr float header_spacing = 6.0f;
inline constexpr float extra_height = 8.0f;
inline constexpr float urgency_dot = 6.0f;
inline constexpr float progress_height = 4.0f;
inline constexpr float progress_track_opacity = 0.3f;

// Close button
inline constexpr float close_hit = 40.0f;
inline constexpr float close_trailing_gap = 8.0f;
inline constexpr int close_icon_px = 16;
inline constexpr float close_idle_opacity = 0.4f;
inline constexpr float wrap_width = card_width - card_pad * 2.0f - (close_hit + close_trailing_gap);

// Typography
inline constexpr int app_px = 17;
inline constexpr int summary_px = 23;
inline constexpr int body_px = 20;
inline constexpr float app_opacity = 0.65f;
inline constexpr float body_opacity = 0.72f;
inline constexpr const char *app_fallback = "Notification";

// Animation
inline constexpr float anim_normal_ms = 220.0f;
inline constexpr float anim_exit_buffer_ms = 60.0f;
inline constexpr float slide_offset = 24.0f;

// Timing
inline constexpr std::chrono::milliseconds hang_time{5000};

} // namespace astralia::notification_config
