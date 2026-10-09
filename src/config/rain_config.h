#pragma once

namespace astralia {

enum class RainMode { Matrix,
                      Stiletto };

struct RainParams {
    RainMode mode = RainMode::Matrix;
    bool async_speed = false;

    bool operator==(const RainParams &) const = default;
};

// window size
constexpr int kRainDefaultWindowWidth = 480;
constexpr int kRainDefaultWindowHeight = 600;

// shared rain timing
constexpr float kRainFallIntervalMs = 45.0f;
constexpr float kRainResetChance = 0.025f;
constexpr float kRainFadeAlpha = 0.05f;

// async fall speed
constexpr float kRainAsyncSpeedMin = 0.4f;
constexpr float kRainAsyncSpeedMax = 1.0f;

// matrix sim: font and cell geometry
constexpr float kMatrixRainFontPx = 20.0f;
constexpr float kMatrixRainCellWidth = 12.0f;
constexpr float kMatrixRainCellHeight = 24.0f;
constexpr float kMatrixRainBoldChance = 0.5f;

// stiletto sim: comet row geometry
constexpr float kStilettoRainColumnSpacingPx = 24.0f;
constexpr float kStilettoRainHeadHeightPx = 24.0f;
constexpr float kStilettoRainStepPx = 24.0f;
constexpr float kStilettoRainTrailWidthPx = 3.0f;

} // namespace astralia
