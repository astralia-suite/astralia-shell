#pragma once

namespace astralia::rain_config {

// window size
constexpr int kDefaultWindowWidth = 480;
constexpr int kDefaultWindowHeight = 600;

// shared rain timing
constexpr float kFallIntervalMs = 45.0f;
constexpr float kResetChance = 0.025f;
constexpr float kFadeAlpha = 0.05f;

// async fall speed
constexpr float kAsyncSpeedMin = 0.4f;
constexpr float kAsyncSpeedMax = 1.0f;

// matrix sim: font and cell geometry
constexpr float kMatrixFontPx = 20.0f;
constexpr float kMatrixCellWidth = 12.0f;
constexpr float kMatrixCellHeight = 24.0f;
constexpr float kMatrixBoldChance = 0.5f;

// stiletto sim: comet row geometry
constexpr float kStilettoColumnSpacingPx = 24.0f;
constexpr float kStilettoHeadHeightPx = 24.0f;
constexpr float kStilettoStepPx = 24.0f;
constexpr float kStilettoTrailWidthPx = 3.0f;

} // namespace astralia::rain_config

namespace astralia {

enum class RainMode { Matrix, Stiletto };

struct RainParams {
    RainMode mode = RainMode::Matrix;
    bool async_speed = false;

    bool operator==(const RainParams &) const = default;
};

} // namespace astralia
