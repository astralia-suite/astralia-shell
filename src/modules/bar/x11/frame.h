#pragma once

#include <cairo.h>

#include "modules/bar/style.h"

namespace astralia {

void paint_bar_frame(cairo_t *cr, const BarStyleSpec &spec, const BarFrame &frame, double width, double height);

} // namespace astralia
