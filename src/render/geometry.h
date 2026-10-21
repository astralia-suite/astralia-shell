#pragma once

namespace astralia::ui {

struct Point {
    float x = 0;
    float y = 0;

    bool operator==(const Point &) const = default;
};

struct Box {
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;

    bool operator==(const Box &) const = default;
};

} // namespace astralia::ui
