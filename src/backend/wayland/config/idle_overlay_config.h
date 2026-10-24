#pragma once

#include <cstdint>

namespace astralia::idle_overlay_config {

// overlay fade
constexpr float fade_ms = 400.0f;
constexpr uint64_t ambient_fade_owner = 1;
constexpr uint64_t screensaver_fade_owner = 2;

// layer-shell namespace
constexpr const char *layer_namespace = "astralia-shell-idle-overlay";

} // namespace astralia::idle_overlay_config
