#pragma once

#include <cstdint>

namespace astralia::idle_overlay_config {

// overlay fade
constexpr float kFadeMs = 400.0f;
constexpr uint64_t kAmbientFadeOwner = 1;
constexpr uint64_t kScreensaverFadeOwner = 2;

// layer-shell namespace
constexpr const char *kLayerNamespace = "astralia-shell-idle-overlay";

} // namespace astralia::idle_overlay_config
