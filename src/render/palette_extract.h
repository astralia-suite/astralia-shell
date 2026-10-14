#pragma once

#include <cstdint>
#include <optional>

namespace astralia {

struct Seed {
    float hue;
    float chroma;
    float lightness;
};

struct Extraction {
    Seed primary;
    std::optional<Seed> secondary;
    bool found;
};

Extraction extract_seeds(const uint8_t *rgba, int width, int height, bool premultiplied);

} // namespace astralia
