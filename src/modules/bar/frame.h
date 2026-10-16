#pragma once

#include "modules/bar/style.h"

#include "render/canvas.h"

namespace astralia {

void bar_frame_base(ui::Canvas &canvas, const BarStyleSpec &style, const BarFrame &frame,
                    float width, float height);

void bar_frame_overlay(ui::Canvas &canvas, const BarStyleSpec &style, const BarFrame &frame,
                       float width, float height, float hug_radius, bool left_flush,
                       bool right_flush);

} // namespace astralia
