#pragma once

#include <array>
#include <chrono>
#include <cstddef>

#include "config/bar_style.h"

namespace astralia::bar_config {

inline constexpr std::size_t style_count = 2;
inline constexpr std::array<BarStyle, style_count> supported_styles{BarStyle::continuous, BarStyle::okinami};

inline constexpr std::chrono::milliseconds trim_interval = std::chrono::minutes(1);

} // namespace astralia::bar_config
