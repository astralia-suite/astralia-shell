#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace astralia {

enum class BarStyle : std::size_t { islands,
                                    okinami,
                                    continuous };

namespace bar_style {

inline constexpr std::size_t count = 3;
inline constexpr std::array<std::string_view, count> names{"islands", "okinami", "continuous"};
inline constexpr std::array<std::string_view, count> labels{"Islands", "Okinami", "Continuous"};

} // namespace bar_style

} // namespace astralia
