#include "check.h"
#include <cmath>

#include "config/lock_config.h"

#include "modules/lock/layout.h"

static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

void check_lock_layout() {
    test::check(near(lock_icon_box_size(), kLockFontIcon + kLockIconBoxMargin * 4.0f), "lock layout");

    test::check(near(lock_card_height(1440.0f), 1440.0f * kLockCardHeightMult), "lock layout");
    test::check(near(lock_card_width(1440.0f), 1440.0f * kLockCardHeightMult * kLockCardRatio), "lock layout");

    test::check(near(lock_center_scale(1440.0f), 1.0f), "lock layout");
    test::check(near(lock_center_scale(720.0f), 0.5f), "lock layout");
    test::check(near(lock_center_scale(2160.0f), 1.0f), "lock layout");

    float cw = lock_card_width(1440.0f);
    float ch = lock_card_height(1440.0f);
    LockRect left, center, right;
    lock_columns(cw, ch, kLockCenterWidth, left, center, right);
    test::check(near(left.x, kLockPanelGap), "lock layout");
    test::check(near(center.w, kLockCenterWidth), "lock layout");
    test::check(near(left.w, right.w), "lock layout");
    test::check(near(center.x, left.x + left.w + kLockPanelGap), "lock layout");
    test::check(near(right.x, center.x + center.w + kLockPanelGap), "lock layout");
    test::check(near(right.x + right.w, cw - kLockPanelGap), "lock layout");
    test::check(near(left.h, ch - 2.0f * kLockPanelGap), "lock layout");

    float sc = lock_side_card_height(600.0f);
    test::check(near(sc * 2.0f + kLockPanelGap, 600.0f), "lock layout");

    float h = lock_content_height(120.0f, 34.0f, 20.0f);
    test::check(near(h, 120.0f + kLockGapClockDate + 34.0f + kLockGapDateAvatar + kLockProfileSize + kLockGapAvatarInput + kLockInputHeight + kLockGapInputMessage + 20.0f), "lock layout");

    test::check(lock_fetch_colour_count(0.0f, 8) == 0, "lock layout");
    test::check(lock_fetch_colour_count(kLockFetchColorBox, 8) == 1, "lock layout");
    test::check(lock_fetch_colour_count(1000.0f, 8) == 8, "lock layout");

    test::check(near(lock_dot_row_width(3), 3.0f * kLockDotSize), "lock layout");
    float w = 400.0f;
    float x0 = lock_dot_x(0, 4, w);
    float x1 = lock_dot_x(1, 4, w);
    test::check(near(x1 - x0, kLockDotSize), "lock layout");
    test::check(near(x0, (w - 4.0f * kLockDotSize) * 0.5f), "lock layout");
    test::check(near(lock_dot_row_width(0), 0.0f), "lock layout");

    float px = 0, py = 0;
    lock_panel_origin(1920.0f, 1080.0f, 540.0f, 400.0f, px, py);
    test::check(near(px, (1920.0f - 540.0f) * 0.5f), "lock layout");
    test::check(near(py, (1080.0f - 400.0f) * 0.5f), "lock layout");
}
