#pragma once

#include <functional>

#include "modules/lock/slash.h"

#include "render/canvas.h"

namespace astralia::lock {

struct Thunder {
    std::function<void(const SlashStrike &)> strike;
    std::function<void(const SlashShock &)> shock;
};

enum class TransitionMode { lock, unlock };

void paint_transition(ui::Canvas &canvas, const SlashPlan &plan, float progress,
                      const std::function<void()> &paint_base, const Thunder &thunder = {},
                      TransitionMode mode = TransitionMode::lock);

} // namespace astralia::lock
