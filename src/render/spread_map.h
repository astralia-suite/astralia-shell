#pragma once

#include <cstdint>
#include <vector>

namespace astralia {

// Grid
inline constexpr int spread_map_width = 480;
inline constexpr int spread_map_height = 270;

struct SpreadMap {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

SpreadMap make_spread_map(uint32_t seed);

} // namespace astralia
