#include <cmath>
#include <cstdlib>

#include "config/lock_config.h"

#include "modules/lock/layout.h"

#include "check.h"

static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

void check_lock_layout() {
    test::check(
        near(astralia::lock::icon_box_size(),
             astralia::lock_config::font_icon + astralia::lock_config::icon_box_margin * 4.0f),
        "lock layout");

    test::check(near(astralia::lock::card_height(1440.0f),
                     1440.0f * astralia::lock_config::card_height_mult),
                "lock layout");
    test::check(
        near(astralia::lock::card_width(1440.0f),
             1440.0f * astralia::lock_config::card_height_mult * astralia::lock_config::card_ratio),
        "lock layout");

    test::check(near(astralia::lock::center_scale(1440.0f), 1.0f), "lock layout");
    test::check(near(astralia::lock::center_scale(720.0f), 0.5f), "lock layout");
    test::check(near(astralia::lock::center_scale(2160.0f), 1.0f), "lock layout");

    float cw = astralia::lock::card_width(1440.0f);
    float ch = astralia::lock::card_height(1440.0f);
    astralia::lock::Rect left, center, right;
    astralia::lock::columns(cw, ch, astralia::lock_config::center_width, left, center, right);
    test::check(near(left.x, astralia::lock_config::panel_gap), "lock layout");
    test::check(near(center.w, astralia::lock_config::center_width), "lock layout");
    test::check(near(left.w, right.w), "lock layout");
    test::check(near(center.x, left.x + left.w + astralia::lock_config::panel_gap), "lock layout");
    test::check(near(right.x, center.x + center.w + astralia::lock_config::panel_gap),
                "lock layout");
    test::check(near(right.x + right.w, cw - astralia::lock_config::panel_gap), "lock layout");
    test::check(near(left.h, ch - 2.0f * astralia::lock_config::panel_gap), "lock layout");

    float sc = astralia::lock::side_card_height(600.0f);
    test::check(near(sc * 2.0f + astralia::lock_config::panel_gap, 600.0f), "lock layout");

    float h = astralia::lock::content_height(120.0f, 34.0f, 20.0f);
    test::check(
        near(h, 120.0f + astralia::lock_config::gap_clock_date + 34.0f +
                    astralia::lock_config::gap_date_avatar + astralia::lock_config::profile_size +
                    astralia::lock_config::gap_avatar_input + astralia::lock_config::input_height +
                    astralia::lock_config::gap_input_message + 20.0f),
        "lock layout");

    setenv("ASTRALIA_DEBUG_UNLOCK", "1", 1);
    test::check(astralia::lock::dev_unlock_hit(0.0, 1200.0, 1200.0) &&
                    astralia::lock::dev_unlock_hit(140.0, 1152.0, 1200.0),
                "the bottom-left corner unlocks");
    test::check(!astralia::lock::dev_unlock_hit(141.0, 1190.0, 1200.0) &&
                    !astralia::lock::dev_unlock_hit(10.0, 1151.0, 1200.0) &&
                    !astralia::lock::dev_unlock_hit(500.0, 600.0, 1200.0),
                "nothing outside the corner unlocks");

    test::check(astralia::lock::fetch_colour_count(0.0f, 8) == 0, "lock layout");
    test::check(astralia::lock::fetch_colour_count(astralia::lock_config::fetch_color_box, 8) == 1,
                "lock layout");
    test::check(astralia::lock::fetch_colour_count(1000.0f, 8) == 8, "lock layout");

    test::check(near(astralia::lock::dot_row_width(3), 3.0f * astralia::lock_config::dot_size),
                "lock layout");
    float w = 400.0f;
    float x0 = astralia::lock::dot_x(0, 4, w);
    float x1 = astralia::lock::dot_x(1, 4, w);
    test::check(near(x1 - x0, astralia::lock_config::dot_size), "lock layout");
    test::check(near(x0, (w - 4.0f * astralia::lock_config::dot_size) * 0.5f), "lock layout");
    test::check(near(astralia::lock::dot_row_width(0), 0.0f), "lock layout");

    float px = 0, py = 0;
    astralia::lock::panel_origin(1920.0f, 1080.0f, 540.0f, 400.0f, px, py);
    test::check(near(px, (1920.0f - 540.0f) * 0.5f), "lock layout");
    test::check(near(py, (1080.0f - 400.0f) * 0.5f), "lock layout");
}
