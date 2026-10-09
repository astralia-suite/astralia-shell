#pragma once

#include "config/logout_config.h"

constexpr int kLogoutButtonCount = astralia::logout_config::button_count;
constexpr float kLogoutButtonSize = static_cast<float>(astralia::logout_config::button_size);
constexpr float kLogoutLogoSize = static_cast<float>(astralia::logout_config::logo_size);
constexpr float kLogoutBorderWidth = static_cast<float>(astralia::logout_config::border_width);
constexpr float kLogoutButtonsRadius = static_cast<float>(astralia::logout_config::ring_radius);

constexpr float kLogoutSlashOvershoot = 0.3f;

constexpr float kLogoutFinishSpan = kLogoutButtonsRadius * 5.2f;
constexpr float kLogoutFinishRise = 230.0f;
constexpr float kLogoutFinishThick = 3.4f;
constexpr float kLogoutFinishSweep = 2.5f;
constexpr float kLogoutFinishIntensity = 2.8f;
constexpr float kLogoutFinishLingerIntensity = 1.4f;

constexpr float kLogoutBurstRingMax = kLogoutButtonsRadius * 1.5f;
constexpr float kLogoutBoltAmp = 13.0f;
