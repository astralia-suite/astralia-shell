#pragma once

#include "config/logout_config.h"

namespace astralia::logout {

constexpr int kButtonCount = astralia::logout_config::button_count;
constexpr float kButtonSize = static_cast<float>(astralia::logout_config::button_size);
constexpr float kButtonsRadius = static_cast<float>(astralia::logout_config::ring_radius);

constexpr float kSlashOvershoot = 0.3f;

constexpr float kFinishSpan = kButtonsRadius * 5.2f;
constexpr float kFinishRise = 230.0f;
constexpr float kFinishThick = 3.4f;
constexpr float kFinishSweep = 2.5f;
constexpr float kFinishIntensity = 2.8f;
constexpr float kFinishLingerIntensity = 1.4f;

constexpr float kBurstRingMax = kButtonsRadius * 1.5f;
constexpr float kBoltAmp = 13.0f;

} // namespace astralia::logout
