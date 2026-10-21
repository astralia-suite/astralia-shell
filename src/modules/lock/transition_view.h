#pragma once

#include <functional>

#include "modules/lock/slash.h"

#include "render/canvas.h"

namespace astralia {

struct LockThunder {
    std::function<void(const SlashStrike &)> strike;
    std::function<void(const SlashShock &)> shock;
};

void paint_lock_transition(ui::Canvas &canvas, const SlashPlan &plan, float progress,
                           const std::function<void()> &paint_base,
                           const LockThunder &thunder = {});

} // namespace astralia
