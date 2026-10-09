#include <cmath>

#include "check.h"

#include "modules/logout/layout.h"

using test::check;

void check_logout_layout() {
    using astralia::logout_button_at;
    using astralia::logout_button_center;
    astralia::Point center{500.0, 400.0};
    astralia::Point top = logout_button_center(0, center);
    check(std::abs(top.x - 500.0) < 1e-6 && std::abs(top.y - 100.0) < 1e-6,
          "first button sits straight above the center");
    astralia::Point right = logout_button_center(2, center);
    check(std::abs(right.x - 800.0) < 1e-6 && std::abs(right.y - 400.0) < 1e-6,
          "third button sits right of the center");
    check(logout_button_at({500.0, 100.0}, center) == 0, "button center hits");
    check(logout_button_at({554.0, 154.0}, center) == 0, "button corner hits");
    check(!logout_button_at(center, center), "logo center hits no button");
}
