#pragma once

#include "config/logout_config.h"

namespace astralia::logout {

constexpr int button_count = astralia::logout_config::button_count;
constexpr float button_size = static_cast<float>(astralia::logout_config::button_size);
constexpr float buttons_radius = static_cast<float>(astralia::logout_config::ring_radius);

constexpr float slash_overshoot = 0.3f;

constexpr float finish_span = buttons_radius * 5.2f;
constexpr float finish_rise = 230.0f;
constexpr float finish_thick = 3.4f;
constexpr float finish_sweep = 2.5f;
constexpr float finish_intensity = 2.8f;
constexpr float finish_linger_intensity = 1.4f;

constexpr float burst_ring_max = buttons_radius * 1.5f;
constexpr float bolt_amp = 13.0f;

} // namespace astralia::logout
