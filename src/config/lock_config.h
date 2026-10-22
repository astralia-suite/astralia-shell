#pragma once

#include <cstdint>

namespace astralia::lock_config {

// development unlock corner
inline constexpr float kDevUnlockWidth = 140.0f;
inline constexpr float kDevUnlockHeight = 48.0f;

// layer namespace
inline constexpr const char *kLayerNamespace = "astralia-shell-lock";

// background & dot
inline constexpr float kBgBorderWidth = 5.0f;

inline constexpr float kDotSize = 18.0f;

// fonts
inline constexpr float kFontIcon = 200.0f;
inline constexpr float kFontClock = 120.0f;
inline constexpr float kFontDate = 34.0f;
inline constexpr float kFontNormal = 20.0f;
inline constexpr float kFontMono = 18.0f;

// card layout
inline constexpr float kCardHeightMult = 0.7f;
inline constexpr float kCardRatio = 16.0f / 9.0f;
inline constexpr float kPanelGap = 25.0f;
inline constexpr float kCenterWidth = 560.0f;
inline constexpr float kCenterRefHeight = 1440.0f;

// icon box
inline constexpr float kIconBoxMargin = 20.0f;

// clock/date/avatar/input gaps
inline constexpr float kClockGap = 10.0f;
inline constexpr float kGapClockDate = 6.0f;
inline constexpr float kGapDateAvatar = 40.0f;
inline constexpr float kGapAvatarInput = 34.0f;
inline constexpr float kGapInputMessage = 18.0f;

// input & pill
inline constexpr float kInputHeight = 56.0f;
inline constexpr float kInputWidthFrac = 0.85f;
inline constexpr float kPillPad = 12.0f;
inline constexpr float kPillIconSize = 26.0f;
inline constexpr float kPillButtonSize = 44.0f;

// profile
inline constexpr float kProfileBorderWidth = 4.0f;
inline constexpr float kProfileSize = 180.0f;

// side panel
inline constexpr float kSidePanelPad = 20.0f;

// fetch
inline constexpr float kFetchLineGap = 6.0f;
inline constexpr float kFetchChipPad = 8.0f;
inline constexpr float kFetchColorBox = 26.0f;
inline constexpr float kFetchColorGap = 10.0f;

// card chrome
inline constexpr float kCardBorderWidth = 2.0f;
inline constexpr float kCardHeaderGap = 12.0f;
inline constexpr float kCardTagPad = 6.0f;
inline constexpr float kBatteryRowGap = 8.0f;
inline constexpr float kBatteryIconGap = 10.0f;
inline constexpr float kBatteryBarHeight = 6.0f;
inline constexpr float kBatteryBarRadius = 3.0f;

// media
inline constexpr float kMediaArt = 116.0f;
inline constexpr float kMediaBtnSize = 40.0f;
inline constexpr float kMediaBtnGap = 12.0f;
inline constexpr float kMediaTextGap = 4.0f;

// resource gauges
inline constexpr float kResTileGap = 14.0f;
inline constexpr float kResIconSize = 26.0f;
inline constexpr float kResValueFont = 32.0f;
inline constexpr float kResTempWarnC = 85.0f;
inline constexpr float kResGaugeStrokeRatio = 6.0f / 68.0f;
inline constexpr float kResGaugeIconValueGap = 2.0f;
inline constexpr float kResGaugeLabelGap = 6.0f;
inline constexpr const char *kResGaugeGpuColorHex = "#3FB6C8";

// notifications
inline constexpr float kNotifCardGap = 8.0f;
inline constexpr float kNotifCardPad = 12.0f;
inline constexpr int kNotifMaxCards = 6;

// scale state
inline constexpr float kScaleHidden = 0.0f;
inline constexpr float kScaleFull = 1.0f;

// animation durations
inline constexpr float kAnimFastMs = 100.0f;

// transition timeline in milliseconds
inline constexpr float kSlashMs = 2800.0f;
inline constexpr int kStarCount = 5;
inline constexpr int kFlurryCount = 14;
inline constexpr int kSlashCount = kStarCount + kFlurryCount;
inline constexpr float kSlashFirstStartMs = 40.0f;
inline constexpr float kSlashStrikeMs = 260.0f;
inline constexpr float kStarAdvanceMs = 100.0f;
inline constexpr float kSlashWaitMs = 600.0f;
inline constexpr float kSlashFinalDrawMs = 70.0f;
inline constexpr float kSlashFinalFadeMs = 280.0f;
inline constexpr float kSlashImpactMs = 60.0f;
inline constexpr float kSlashShockMs = 200.0f;
inline constexpr float kSlashFlashHoldMs = 60.0f;
inline constexpr float kSlashFlashMs = 400.0f;
inline constexpr float kSlashFinalBeamMs = 280.0f;
inline constexpr float kSlashSliverMs = 420.0f;
inline constexpr float kSlashDimRampMs = 300.0f;
inline constexpr float kSlashDimLiftMs = 320.0f;
inline constexpr float kSlashShatterHoldMs = 25.0f;
inline constexpr float kSlashShatterSpreadMs = 200.0f;
inline constexpr float kSlashFallMs = 320.0f;

// transition star
struct StarCorner {
    float x;
    float y;
};

inline constexpr StarCorner kStarCorners[kStarCount] = {
    {0.50f, 0.24f}, {0.78f, 0.77f}, {0.24f, 0.36f}, {0.80f, 0.36f}, {0.16f, 0.79f},
};

// transition flurry
struct FlurrySlash {
    float degrees;
    float offset;
    float start_ms;
};

inline constexpr float kReferenceWidth = 16.0f;
inline constexpr float kReferenceHeight = 9.0f;
inline constexpr FlurrySlash kFlurrySlashes[kFlurryCount] = {
    {-89.0f, 0.28f, 480.0f},  {34.0f, 0.16f, 560.0f},    {-10.0f, -0.22f, 720.0f},
    {-32.0f, 0.16f, 800.0f},  {-63.0f, -0.29f, 830.0f},  {11.0f, 0.26f, 880.0f},
    {77.0f, -0.30f, 910.0f},  {53.0f, 0.34f, 960.0f},    {-79.0f, -0.34f, 1040.0f},
    {69.0f, 0.30f, 1070.0f},  {-71.0f, -0.30f, 1120.0f}, {-55.0f, 0.23f, 1200.0f},
    {85.0f, -0.29f, 1220.0f}, {61.0f, 0.25f, 1240.0f},
};

// transition timeline as shares of progress
inline constexpr float kSlashFirstStart = kSlashFirstStartMs / kSlashMs;
inline constexpr float kSlashDrawSpan = kSlashStrikeMs / kSlashMs;
inline constexpr float kSlashFinalStart =
    (kSlashFirstStartMs + kFlurrySlashes[kFlurryCount - 1].start_ms + kSlashStrikeMs +
     kSlashWaitMs) /
    kSlashMs;
inline constexpr float kSlashFinalDrawSpan = kSlashFinalDrawMs / kSlashMs;
inline constexpr float kSlashFinalFade = kSlashFinalFadeMs / kSlashMs;
inline constexpr float kSlashImpactSpan = kSlashImpactMs / kSlashMs;
inline constexpr float kSlashShockSpan = kSlashShockMs / kSlashMs;
inline constexpr float kSlashFlashHold = kSlashFlashHoldMs / kSlashMs;
inline constexpr float kSlashFlashSpan = kSlashFlashMs / kSlashMs;
inline constexpr float kSlashFinalBeamSpan = kSlashFinalBeamMs / kSlashMs;
inline constexpr float kSlashSliverSpan = kSlashSliverMs / kSlashMs;
inline constexpr float kSlashDimRamp = kSlashDimRampMs / kSlashMs;
inline constexpr float kSlashDimLift = kSlashDimLiftMs / kSlashMs;
inline constexpr float kSlashShatterHold = kSlashShatterHoldMs / kSlashMs;
inline constexpr float kSlashShatterSpread = kSlashShatterSpreadMs / kSlashMs;
inline constexpr float kSlashFallSpan = kSlashFallMs / kSlashMs;

// transition geometry
inline constexpr float kSlashGapFrac = 0.012f;
inline constexpr float kSlashMinPieceArea = 1.0f;
inline constexpr float kSlashFinalReach = 0.05f;
inline constexpr float kFinalTilt = 0.14f;
inline constexpr int kSliverCount = 36;
inline constexpr float kSliverLengthMin = 24.0f;
inline constexpr float kSliverLengthMax = 80.0f;
inline constexpr float kSliverThickMin = 2.0f;
inline constexpr float kSliverThickMax = 4.0f;
inline constexpr float kSliverSpreadFrac = 0.14f;
inline constexpr float kSliverDriftFrac = 0.10f;
inline constexpr float kSliverAlongFrac = 0.12f;
inline constexpr float kSlashStrikeOvershoot = 0.05f;
inline constexpr float kSlashShardDrift = 0.06f;
inline constexpr float kSlashShardSpin = 0.25f;
inline constexpr float kSlashShardRankNoise = 0.25f;
inline constexpr int kSlashRayCount = 18;
inline constexpr float kSlashRayHalfAngle = 0.012f;

// transition thunder
inline constexpr float kStrikeAmp = 13.0f;
inline constexpr float kStrikePeak = 1.2f;
inline constexpr float kFinalThick = 3.4f;
inline constexpr float kFinalAmpMult = 2.4f;
inline constexpr float kFinalIntensity = 2.8f;
inline constexpr float kFinalSeed = 21.0f;
inline constexpr float kShockRadiusFrac = 0.6f;
inline constexpr float kShockIntensity = 1.3f;

// transition paint
inline constexpr float kCutmarkWidth = 4.0f;
inline constexpr float kCutmarkAlpha = 0.85f;
inline constexpr float kFallbackBoltWidth = 3.0f;
inline constexpr float kBoltGlowNearScale = 2.8f;
inline constexpr float kBoltGlowFarScale = 6.0f;
inline constexpr float kBoltGlowNearAlpha = 0.35f;
inline constexpr float kBoltGlowFarAlpha = 0.15f;
inline constexpr float kSlashRayAlpha = 0.5f;
inline constexpr float kSlashFlashPeak = 0.9f;
inline constexpr float kDimPeak = 0.5f;
inline constexpr const char *kDimHex = "#0B0620";
inline constexpr const char *kFlashHex = "#EDE6FF";
inline constexpr float kBeamPeakFrac = 0.05f;
inline constexpr float kFinalBeamFrac = 0.3f;
inline constexpr float kFinalThinFrac = 0.012f;
inline constexpr float kBeamGlowScale = 1.8f;
inline constexpr float kBeamGlowAlpha = 0.3f;
inline constexpr float kSliverAlpha = 0.9f;

// fail timer
inline constexpr float kTimerFailMs = 3000.0f;

// text strings
inline constexpr const char *kFailText = "Skill Issue";
inline constexpr const char *kPlaceholderText = "Enter your password";
inline constexpr const char *kLoadingText = "Loading...";

// x11 tick
inline constexpr int kTickMs = 1000;
inline constexpr int kGrabAttempts = 100;
inline constexpr int kGrabRetryMs = 10;

enum AnimOwner : uint64_t {
    kOwnerDotRowX = 8,
    kOwnerAvatarFrame = 10,
    kOwnerDotBase = 1000,
};

} // namespace astralia::lock_config
