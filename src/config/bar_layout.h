#pragma once

#include <chrono>
#include <cstdint>

namespace astralia::bar_layout {

inline constexpr float height = 40.0f;
inline constexpr int top_margin = 10;
inline constexpr int side_margin = 20;
inline constexpr int panel_gap = 8;

inline constexpr float icon_px = 18.0f;
inline constexpr int text_px = 17;
inline constexpr float label_gap = 10.0f;
inline constexpr float capsule_gap = 10.0f;
inline constexpr float island_pad = 6.0f;
inline constexpr float divider_height_ratio = 0.4f;
inline constexpr float divider_width = 1.0f;
inline constexpr float expand_ms = 150.0f;
inline constexpr std::chrono::milliseconds close_linger{80};

inline constexpr float workspace_pill_height = 12.0f;
inline constexpr float workspace_pill_spacing = 5.0f;
inline constexpr float workspace_active_scale = 2.0f;
inline constexpr float workspace_anim_ms = 100.0f;
inline constexpr float workspace_overview_gap = 8.0f;

inline constexpr float dock_icon = 22.0f;
inline constexpr float dock_spacing = 10.0f;
inline constexpr float dock_focused_alpha = 1.0f;
inline constexpr float dock_unfocused_alpha = 0.5f;
inline constexpr float dock_reorder_ms = 200.0f;

inline constexpr std::chrono::milliseconds peek_duration{2000};
inline constexpr std::chrono::milliseconds peek_ready_delay{1000};

inline constexpr int battery_critical_percent = 10;

inline constexpr uint64_t owner_expand = 100;
inline constexpr uint64_t owner_workspace = 200;
inline constexpr uint64_t owner_dock = 300;

inline constexpr const char *clock_format = "%a %Y-%m-%d %H:%M:%S";

} // namespace astralia::bar_layout
