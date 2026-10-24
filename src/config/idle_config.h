#pragma once

#include <cstdint>

namespace astralia::idle_config {

// activity pulse
constexpr uint32_t recent_activity_pulse_seconds = 2;

// screensaver logo
constexpr float logo_speed = 90.0f;
constexpr float logo_size = 200.0f;
constexpr const char *logo_asset = "stellar-restoration.svg";

} // namespace astralia::idle_config
