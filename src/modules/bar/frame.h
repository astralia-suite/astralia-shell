#pragma once

#include "modules/bar/style.h"

#include "render/canvas.h"

namespace astralia::bar {

void frame_base(ui::Canvas &canvas, const StyleSpec &style, const Frame &frame, float width,
                float height);

void frame_overlay(ui::Canvas &canvas, const StyleSpec &style, const Frame &frame, float width,
                   float height, float hug_radius, bool left_flush, bool right_flush);

} // namespace astralia::bar
