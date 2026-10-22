#pragma once

#include <cstdint>

namespace astralia::bamboo_config {

// window size
constexpr int kDefaultWindowWidth = 560;
constexpr int kDefaultWindowHeight = 600;

// timing
constexpr float kTickIntervalMs = 80.0f;
constexpr int kHoldTicks = 60;
constexpr float kFadeStep = 0.1f;

// character grid
constexpr float kCellWidthPx = 10.0f;
constexpr float kCellHeightPx = 20.0f;
constexpr float kFontPx = 18.0f;

// ground and shoots
constexpr int kGroundMarginRows = 1;
constexpr int kGroundInsetCols = 2;
constexpr int kShootCount = 14;
constexpr int kShootMaxRows = 2;

// culms
constexpr int kCulmCount = 8;
constexpr float kCulmMinHeightRatio = 0.5f;
constexpr float kCulmMaxHeightRatio = 0.92f;
constexpr char32_t kCulmGlyph = U'│';
constexpr int kNodeSpacingMinRows = 2;
constexpr int kNodeSpacingMaxRows = 3;

// branches and leaves
constexpr float kBranchZoneRatio = 0.4f;
constexpr float kBranchChance = 0.75f;
constexpr int kBranchMinLength = 4;
constexpr int kBranchMaxLength = 8;
constexpr float kBranchFlatChance = 0.6f;
constexpr int kLeafSpacing = 1;
constexpr char32_t kLeafGlyphRight = U'>';
constexpr char32_t kLeafGlyphLeft = U'<';

// colors as 0xAARRGGBB
constexpr uint32_t kGroundColor = 0xFFA08A6E;
constexpr uint32_t kShootColor = 0xFFC8D88A;
constexpr uint32_t kCulmColor = 0xFF6FA84F;
constexpr uint32_t kNodeColor = 0xFF3F8A52;
constexpr uint32_t kBranchColor = 0xFF5E9B4A;
constexpr uint32_t kLeafColor = 0xFF8FD16A;

} // namespace astralia::bamboo_config
