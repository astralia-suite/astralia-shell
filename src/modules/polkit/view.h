#pragma once

#include "modules/polkit/model.h"

#include "render/canvas.h"

namespace astralia {

void paint_polkit(ui::Canvas &canvas, PolkitModel &model, float surface_width, float surface_height);

} // namespace astralia
