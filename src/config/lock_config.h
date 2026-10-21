#pragma once

#include <cstdint>

// development unlock corner
inline constexpr float kLockDevUnlockWidth = 140.0f;
inline constexpr float kLockDevUnlockHeight = 48.0f;

// layer namespace
inline constexpr const char *kLockLayerNamespace = "astralia-shell-lock";

// background & dot
inline constexpr float kLockBgBorderWidth = 5.0f;

inline constexpr float kLockDotSize = 18.0f;

// fonts
inline constexpr float kLockFontIcon = 200.0f;
inline constexpr float kLockFontClock = 120.0f;
inline constexpr float kLockFontDate = 34.0f;
inline constexpr float kLockFontNormal = 20.0f;
inline constexpr float kLockFontMono = 18.0f;

// card layout
inline constexpr float kLockCardHeightMult = 0.7f;
inline constexpr float kLockCardRatio = 16.0f / 9.0f;
inline constexpr float kLockPanelGap = 25.0f;
inline constexpr float kLockCenterWidth = 560.0f;
inline constexpr float kLockCenterRefHeight = 1440.0f;

// icon box
inline constexpr float kLockIconBoxMargin = 20.0f;

// clock/date/avatar/input gaps
inline constexpr float kLockClockGap = 10.0f;
inline constexpr float kLockGapClockDate = 6.0f;
inline constexpr float kLockGapDateAvatar = 40.0f;
inline constexpr float kLockGapAvatarInput = 34.0f;
inline constexpr float kLockGapInputMessage = 18.0f;

// input & pill
inline constexpr float kLockInputHeight = 56.0f;
inline constexpr float kLockInputWidthFrac = 0.85f;
inline constexpr float kLockPillPad = 12.0f;
inline constexpr float kLockPillIconSize = 26.0f;
inline constexpr float kLockPillButtonSize = 44.0f;

// profile
inline constexpr float kLockProfileBorderWidth = 4.0f;
inline constexpr float kLockProfileSize = 180.0f;

// side panel
inline constexpr float kLockSidePanelPad = 20.0f;

// fetch
inline constexpr float kLockFetchLineGap = 6.0f;
inline constexpr float kLockFetchChipPad = 8.0f;
inline constexpr float kLockFetchColorBox = 26.0f;
inline constexpr float kLockFetchColorGap = 10.0f;

// card chrome
inline constexpr float kLockCardBorderWidth = 2.0f;
inline constexpr float kLockCardHeaderGap = 12.0f;
inline constexpr float kLockCardTagPad = 6.0f;
inline constexpr float kLockBatteryRowGap = 8.0f;
inline constexpr float kLockBatteryIconGap = 10.0f;
inline constexpr float kLockBatteryBarHeight = 6.0f;
inline constexpr float kLockBatteryBarRadius = 3.0f;

// media
inline constexpr float kLockMediaArt = 116.0f;
inline constexpr float kLockMediaBtnSize = 40.0f;
inline constexpr float kLockMediaBtnGap = 12.0f;
inline constexpr float kLockMediaTextGap = 4.0f;

// resource gauges
inline constexpr float kLockResTileGap = 14.0f;
inline constexpr float kLockResIconSize = 26.0f;
inline constexpr float kLockResValueFont = 32.0f;
inline constexpr float kLockResTempWarnC = 85.0f;
inline constexpr float kLockResGaugeStrokeRatio = 6.0f / 68.0f;
inline constexpr float kLockResGaugeIconValueGap = 2.0f;
inline constexpr float kLockResGaugeLabelGap = 6.0f;
inline constexpr const char *kLockResGaugeGpuColorHex = "#3FB6C8";

// notifications
inline constexpr float kLockNotifCardGap = 8.0f;
inline constexpr float kLockNotifCardPad = 12.0f;
inline constexpr int kLockNotifMaxCards = 6;

// scale state
inline constexpr float kLockScaleHidden = 0.0f;
inline constexpr float kLockScaleFull = 1.0f;

// animation durations
inline constexpr float kLockAnimFastMs = 100.0f;

// transition timeline in milliseconds
inline constexpr float kLockSlashMs = 2800.0f;
inline constexpr int kLockStarCount = 5;
inline constexpr int kLockFlurryCount = 14;
inline constexpr int kLockSlashCount = kLockStarCount + kLockFlurryCount;
inline constexpr float kLockSlashFirstStartMs = 40.0f;
inline constexpr float kLockSlashStrikeMs = 260.0f;
inline constexpr float kLockStarAdvanceMs = 100.0f;
inline constexpr float kLockSlashWaitMs = 600.0f;
inline constexpr float kLockSlashFinalDrawMs = 70.0f;
inline constexpr float kLockSlashFinalFadeMs = 280.0f;
inline constexpr float kLockSlashImpactMs = 60.0f;
inline constexpr float kLockSlashShockMs = 200.0f;
inline constexpr float kLockSlashFlashHoldMs = 60.0f;
inline constexpr float kLockSlashFlashMs = 400.0f;
inline constexpr float kLockSlashFinalBeamMs = 280.0f;
inline constexpr float kLockSlashSliverMs = 420.0f;
inline constexpr float kLockSlashDimRampMs = 300.0f;
inline constexpr float kLockSlashDimLiftMs = 320.0f;
inline constexpr float kLockSlashShatterHoldMs = 25.0f;
inline constexpr float kLockSlashShatterSpreadMs = 200.0f;
inline constexpr float kLockSlashFallMs = 320.0f;

// transition star
struct LockStarCorner {
    float x;
    float y;
};

inline constexpr LockStarCorner kLockStarCorners[kLockStarCount] = {
    {0.50f, 0.24f}, {0.78f, 0.77f}, {0.24f, 0.36f}, {0.80f, 0.36f}, {0.16f, 0.79f},
};

// transition flurry
struct LockFlurrySlash {
    float degrees;
    float offset;
    float start_ms;
};

inline constexpr float kLockReferenceWidth = 16.0f;
inline constexpr float kLockReferenceHeight = 9.0f;
inline constexpr LockFlurrySlash kLockFlurrySlashes[kLockFlurryCount] = {
    {-89.0f, 0.00f, 480.0f},  {34.0f, 0.10f, 560.0f},    {-10.0f, -0.12f, 720.0f},
    {-32.0f, 0.16f, 800.0f},  {-63.0f, -0.22f, 830.0f},  {11.0f, 0.26f, 880.0f},
    {77.0f, -0.30f, 910.0f},  {53.0f, 0.34f, 960.0f},    {-79.0f, -0.34f, 1040.0f},
    {69.0f, 0.30f, 1070.0f},  {-71.0f, -0.26f, 1120.0f}, {-55.0f, 0.22f, 1200.0f},
    {85.0f, -0.16f, 1220.0f}, {61.0f, 0.12f, 1240.0f},
};

// transition timeline as shares of progress
inline constexpr float kLockSlashFirstStart = kLockSlashFirstStartMs / kLockSlashMs;
inline constexpr float kLockSlashDrawSpan = kLockSlashStrikeMs / kLockSlashMs;
inline constexpr float kLockSlashFinalStart =
    (kLockSlashFirstStartMs + kLockFlurrySlashes[kLockFlurryCount - 1].start_ms +
     kLockSlashStrikeMs + kLockSlashWaitMs) /
    kLockSlashMs;
inline constexpr float kLockSlashFinalDrawSpan = kLockSlashFinalDrawMs / kLockSlashMs;
inline constexpr float kLockSlashFinalFade = kLockSlashFinalFadeMs / kLockSlashMs;
inline constexpr float kLockSlashImpactSpan = kLockSlashImpactMs / kLockSlashMs;
inline constexpr float kLockSlashShockSpan = kLockSlashShockMs / kLockSlashMs;
inline constexpr float kLockSlashFlashHold = kLockSlashFlashHoldMs / kLockSlashMs;
inline constexpr float kLockSlashFlashSpan = kLockSlashFlashMs / kLockSlashMs;
inline constexpr float kLockSlashFinalBeamSpan = kLockSlashFinalBeamMs / kLockSlashMs;
inline constexpr float kLockSlashSliverSpan = kLockSlashSliverMs / kLockSlashMs;
inline constexpr float kLockSlashDimRamp = kLockSlashDimRampMs / kLockSlashMs;
inline constexpr float kLockSlashDimLift = kLockSlashDimLiftMs / kLockSlashMs;
inline constexpr float kLockSlashShatterHold = kLockSlashShatterHoldMs / kLockSlashMs;
inline constexpr float kLockSlashShatterSpread = kLockSlashShatterSpreadMs / kLockSlashMs;
inline constexpr float kLockSlashFallSpan = kLockSlashFallMs / kLockSlashMs;

// transition geometry
inline constexpr float kLockSlashGapFrac = 0.012f;
inline constexpr float kLockSlashMinPieceArea = 1.0f;
inline constexpr float kLockSlashFinalReach = 0.05f;
inline constexpr float kLockFinalTilt = 0.14f;
inline constexpr int kLockSliverCount = 36;
inline constexpr float kLockSliverLengthMin = 24.0f;
inline constexpr float kLockSliverLengthMax = 80.0f;
inline constexpr float kLockSliverThickMin = 2.0f;
inline constexpr float kLockSliverThickMax = 4.0f;
inline constexpr float kLockSliverSpreadFrac = 0.14f;
inline constexpr float kLockSliverDriftFrac = 0.10f;
inline constexpr float kLockSliverAlongFrac = 0.12f;
inline constexpr float kLockSlashStrikeOvershoot = 0.05f;
inline constexpr float kLockSlashShardDrift = 0.06f;
inline constexpr float kLockSlashShardSpin = 0.25f;
inline constexpr float kLockSlashShardRankNoise = 0.25f;
inline constexpr int kLockSlashRayCount = 18;
inline constexpr float kLockSlashRayHalfAngle = 0.012f;

// transition thunder
inline constexpr float kLockStrikeAmp = 13.0f;
inline constexpr float kLockStrikePeak = 1.2f;
inline constexpr float kLockFinalThick = 3.4f;
inline constexpr float kLockFinalAmpMult = 2.4f;
inline constexpr float kLockFinalIntensity = 2.8f;
inline constexpr float kLockFinalSeed = 21.0f;
inline constexpr float kLockShockRadiusFrac = 0.6f;
inline constexpr float kLockShockIntensity = 1.3f;

// transition paint
inline constexpr float kLockCutmarkWidth = 4.0f;
inline constexpr float kLockCutmarkAlpha = 0.85f;
inline constexpr float kLockFallbackBoltWidth = 3.0f;
inline constexpr float kLockBoltGlowNearScale = 2.8f;
inline constexpr float kLockBoltGlowFarScale = 6.0f;
inline constexpr float kLockBoltGlowNearAlpha = 0.35f;
inline constexpr float kLockBoltGlowFarAlpha = 0.15f;
inline constexpr float kLockSlashRayAlpha = 0.5f;
inline constexpr float kLockSlashFlashPeak = 0.9f;
inline constexpr float kLockDimPeak = 0.5f;
inline constexpr const char *kLockDimHex = "#0B0620";
inline constexpr const char *kLockFlashHex = "#EDE6FF";
inline constexpr float kLockBeamPeakFrac = 0.05f;
inline constexpr float kLockFinalBeamFrac = 0.3f;
inline constexpr float kLockFinalThinFrac = 0.012f;
inline constexpr float kLockBeamGlowScale = 1.8f;
inline constexpr float kLockBeamGlowAlpha = 0.3f;
inline constexpr float kLockSliverAlpha = 0.9f;

// fail timer
inline constexpr float kLockTimerFailMs = 3000.0f;

// text strings
inline constexpr const char *kLockFailText = "Skill Issue";
inline constexpr const char *kLockPlaceholderText = "Enter your password";
inline constexpr const char *kLockLoadingText = "Loading...";

// x11 tick
inline constexpr int kLockTickMs = 1000;
inline constexpr int kLockGrabAttempts = 100;
inline constexpr int kLockGrabRetryMs = 10;

enum LockAnimOwner : uint64_t {
    kLockOwnerDotRowX = 8,
    kLockOwnerAvatarFrame = 10,
    kLockOwnerDotBase = 1000,
};
