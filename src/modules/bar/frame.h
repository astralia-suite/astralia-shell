#pragma once

#include "modules/bar/style.h"

#include "render/canvas.h"

namespace astralia {

// The bar background: the continuous pill, or the rail and island outlines.
void bar_frame_base(ui::Canvas &canvas, const BarStyleSpec &style, const BarFrame &frame, float width, float height);

// The fillets between rail and islands, and the hug corners where an island meets the screen edge. Painted over the items.
void bar_frame_overlay(ui::Canvas &canvas, const BarStyleSpec &style, const BarFrame &frame, float width, float height, float hug_radius, bool left_flush, bool right_flush);

} // namespace astralia
