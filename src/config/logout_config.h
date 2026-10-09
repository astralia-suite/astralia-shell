#pragma once

#include <array>
#include <numbers>

namespace astralia {

struct LogoutAction {
    const char *glyph;
    const char *command;
};

namespace logout_config {

// Geometry
inline constexpr int button_count = 8;
inline constexpr double button_size = 110.0;
inline constexpr double button_corner_radius = button_size / 5.0;
inline constexpr double ring_radius = 300.0;
inline constexpr double border_width = 5.0;
inline constexpr double highlight_scale = 1.05;
inline constexpr int logo_size = 250;

// Typography
inline constexpr int glyph_px = static_cast<int>(button_size / 2.0);

// Choreography
inline constexpr float logo_anim_ms = 600.0f;
inline constexpr float button_scale_ms = 140.0f;
inline constexpr float button_border_ms = 160.0f;
inline constexpr float hold_ms = 300.0f;
inline constexpr float slash_ms = 120.0f;
inline constexpr float slash_advance_frac = 0.75f;
inline constexpr float burst_ms = 500.0f;
inline constexpr float push_ms = 260.0f;
inline constexpr float exit_spread = 0.55f;
inline constexpr float exit_fade_ms = burst_ms * 0.8f;
inline constexpr int star_step = 3;
inline constexpr int lock_index = 2;
inline constexpr const char *lock_command = "astralia lock";
inline constexpr const char *logo_asset = "logout/logo.png";

// Ring angles
inline constexpr double start_angle = -std::numbers::pi / 2.0;
inline constexpr double step_angle = 2.0 * std::numbers::pi / button_count;

// Actions
inline constexpr std::array<LogoutAction, button_count> actions{{
    {"劍", "systemctl poweroff"},
    {"光", "systemctl reboot"},
    {"如", ""},
    {"我", "systemctl reboot --firmware-setup"},
    {"斬", ""},
    {"盡", ""},
    {"蕪", ""},
    {"雜", ""},
}};

} // namespace logout_config

} // namespace astralia
