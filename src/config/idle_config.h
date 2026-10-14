#pragma once

#include <cstdint>

namespace astralia {

// activity pulse
constexpr uint32_t kIdleRecentActivityPulseSeconds = 2;

// screensaver logo
constexpr float kIdleLogoSpeed = 90.0f;
constexpr float kIdleLogoSize = 200.0f;
constexpr const char *kIdleLogoAsset = "stellar-restoration.svg";

} // namespace astralia
