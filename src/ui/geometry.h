#pragma once

namespace astralia::ui {

struct Box {
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;

    bool operator==(const Box &) const = default;
};

} // namespace astralia::ui
