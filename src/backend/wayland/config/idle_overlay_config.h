#pragma once

#include <cstdint>

// overlay fade
constexpr float kIdleOverlayFadeMs = 400.0f;
constexpr uint64_t kIdleAmbientFadeOwner = 1;
constexpr uint64_t kIdleScreensaverFadeOwner = 2;

// layer-shell namespace
constexpr const char *kIdleOverlayLayerNamespace = "astralia-shell-idle-overlay";
