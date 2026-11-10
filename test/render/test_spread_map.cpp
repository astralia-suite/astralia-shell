#include <algorithm>
#include <cstdint>
#include <vector>

#include "render/spread_map.h"

#include "check.h"

namespace {

using astralia::SpreadMap;

uint16_t arrival(const SpreadMap &map, size_t cell) {
    return static_cast<uint16_t>(map.rgba[cell * 4] << 8 | map.rgba[cell * 4 + 1]);
}

uint16_t glow(const SpreadMap &map, size_t cell) {
    return static_cast<uint16_t>(map.rgba[cell * 4 + 2] << 8 | map.rgba[cell * 4 + 3]);
}

} // namespace

void check_spread_map() {
    using test::check;
    SpreadMap a = astralia::make_spread_map(7);
    SpreadMap b = astralia::make_spread_map(7);
    SpreadMap c = astralia::make_spread_map(8);
    size_t cells = static_cast<size_t>(astralia::spread_map_width) * astralia::spread_map_height;

    check(a.width == astralia::spread_map_width && a.height == astralia::spread_map_height &&
              a.rgba.size() == cells * 4,
          "the spread map covers the whole grid in RGBA");
    check(a.rgba == b.rgba, "the same seed gives the same spread");
    check(a.rgba != c.rgba, "another seed gives another spread");

    std::vector<uint16_t> ranks(cells);
    for (size_t i = 0; i < cells; ++i) {
        ranks[i] = arrival(a, i);
    }
    auto [low, high] = std::ranges::minmax(ranks);
    check(low == 0 && high == 65535, "arrival ranks run from the first cell to the last");

    std::vector<uint16_t> sorted = ranks;
    std::ranges::sort(sorted);
    size_t quarter = sorted[cells / 4];
    check(quarter > 65535 / 4 - 512 && quarter < 65535 / 4 + 512,
          "ranks are spread evenly, so the area grows at a steady rate");

    size_t first = static_cast<size_t>(std::ranges::find(ranks, uint16_t{0}) - ranks.begin());
    size_t last = static_cast<size_t>(std::ranges::find(ranks, uint16_t{65535}) - ranks.begin());
    check(glow(a, first) < glow(a, last), "the glow twin follows the spread, low where it starts");
}
