#pragma once

#include <cstdint>

namespace astralia {

// window size
constexpr int kBambooDefaultWindowWidth = 560;
constexpr int kBambooDefaultWindowHeight = 600;

// timing
constexpr float kBambooTickIntervalMs = 80.0f;
constexpr int kBambooHoldTicks = 60;
constexpr float kBambooFadeStep = 0.1f;

// character grid
constexpr float kBambooCellWidthPx = 10.0f;
constexpr float kBambooCellHeightPx = 20.0f;
constexpr float kBambooFontPx = 18.0f;

// ground and shoots
constexpr int kBambooGroundMarginRows = 1;
constexpr int kBambooGroundInsetCols = 2;
constexpr int kBambooShootCount = 14;
constexpr int kBambooShootMaxRows = 2;

// culms
constexpr int kBambooCulmCount = 8;
constexpr float kBambooCulmMinHeightRatio = 0.5f;
constexpr float kBambooCulmMaxHeightRatio = 0.92f;
constexpr char32_t kBambooCulmGlyph = U'│';
constexpr float kBambooLeanChance = 0.12f;
constexpr int kBambooMaxLeanCols = 3;
constexpr int kBambooNodeSpacingMinRows = 2;
constexpr int kBambooNodeSpacingMaxRows = 3;

// branches and leaves
constexpr float kBambooBranchZoneRatio = 0.4f;
constexpr float kBambooBranchChance = 0.75f;
constexpr int kBambooBranchMinLength = 4;
constexpr int kBambooBranchMaxLength = 8;
constexpr float kBambooBranchFlatChance = 0.6f;
constexpr int kBambooLeafSpacing = 1;
constexpr char32_t kBambooLeafGlyphRight = U'>';
constexpr char32_t kBambooLeafGlyphLeft = U'<';

// colors as 0xAARRGGBB
constexpr uint32_t kBambooGroundColor = 0xFFA08A6E;
constexpr uint32_t kBambooShootColor = 0xFFC8D88A;
constexpr uint32_t kBambooCulmColor = 0xFF6FA84F;
constexpr uint32_t kBambooNodeColor = 0xFF3F8A52;
constexpr uint32_t kBambooBranchColor = 0xFF5E9B4A;
constexpr uint32_t kBambooLeafColor = 0xFF8FD16A;

} // namespace astralia
