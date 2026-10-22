#include <cmath>

#include "modules/logout/layout.h"

#include "check.h"

using test::check;

void check_logout_layout() {
    using astralia::logout::button_at;
    using astralia::logout::button_center;
    astralia::logout::Point center{500.0, 400.0};
    astralia::logout::Point top = astralia::logout::button_center(0, center);
    check(std::abs(top.x - 500.0) < 1e-6 && std::abs(top.y - 100.0) < 1e-6,
          "first button sits straight above the center");
    astralia::logout::Point right = astralia::logout::button_center(2, center);
    check(std::abs(right.x - 800.0) < 1e-6 && std::abs(right.y - 400.0) < 1e-6,
          "third button sits right of the center");
    check(astralia::logout::button_at({500.0, 100.0}, center) == 0, "button center hits");
    check(astralia::logout::button_at({554.0, 154.0}, center) == 0, "button corner hits");
    check(!astralia::logout::button_at(center, center), "logo center hits no button");
}
