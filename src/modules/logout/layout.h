#pragma once

#include <optional>

namespace astralia::logout {

struct Point {
    double x;
    double y;
};

Point button_center(int index, Point center);
std::optional<int> button_at(Point pointer, Point center);

} // namespace astralia::logout
