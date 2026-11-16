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
    bool contains(double px, double py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

} // namespace astralia::ui
