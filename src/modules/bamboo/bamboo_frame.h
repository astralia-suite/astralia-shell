#pragma once

#include <cstdint>

namespace astralia {

struct BambooFrame {
    const uint8_t *pixels = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;

    bool ready() const { return pixels != nullptr; }
};

} // namespace astralia
