#pragma once

#include "modules/polkit/model.h"

#include "render/canvas.h"

namespace astralia::polkit {

void paint(ui::Canvas &canvas, Model &model, float surface_width, float surface_height);

} // namespace astralia::polkit
