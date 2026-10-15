#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace astralia {

inline constexpr size_t max_seeds = 4;

struct Seed {
    float hue;
    float chroma;
    float lightness;
};

struct Extraction {
    std::vector<Seed> seeds;
    bool found;
};

Extraction extract_seeds(const uint8_t *rgba, int width, int height, bool premultiplied);

} // namespace astralia
