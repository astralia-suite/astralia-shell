#include <cmath>

#include "config/logout_config.h"

#include "modules/logout/layout.h"

namespace astralia::logout {

Point button_center(int index, Point center) {
    double angle = logout_config::start_angle + logout_config::step_angle * index;
    return {center.x + logout_config::ring_radius * std::cos(angle),
            center.y + logout_config::ring_radius * std::sin(angle)};
}

std::optional<int> button_at(Point pointer, Point center) {
    constexpr double half = logout_config::button_size / 2.0;
    for (int i = 0; i < logout_config::button_count; ++i) {
        Point c = button_center(i, center);
        if (pointer.x >= c.x - half && pointer.x < c.x + half && pointer.y >= c.y - half &&
            pointer.y < c.y + half) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace astralia::logout
