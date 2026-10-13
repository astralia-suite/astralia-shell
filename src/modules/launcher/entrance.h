#pragma once

#include <vector>

#include "render/geometry.h"

namespace astralia {

struct EntranceBolt {
    float ax = 0.0f;
    float ay = 0.0f;
    float bx = 0.0f;
    float by = 0.0f;
    float progress = 1.0f;
    float intensity = 1.0f;
    float seed = 0.0f;
    float amp = 14.0f;
    float thick = 1.0f;
    bool stitch = false;
};

struct EntranceBurst {
    ui::Box box;
    float reach = 0.0f;
    float progress = 0.0f;
    float intensity = 1.0f;
};

struct EntranceState {
    float t = 1.0f;
    float seed = 0.0f;
};

struct EntranceFrame {
    float opacity = 1.0f;
    float scrim = 0.0f;
    float flash = 0.0f;
    float time_s = 0.0f;
    std::vector<EntranceBolt> bolts;
    std::vector<EntranceBurst> bursts;
};

EntranceFrame entrance_sample(const EntranceState &state, const ui::Box &box);

} // namespace astralia
