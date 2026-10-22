#pragma once

#include "modules/bar/model.h"

#include "render/canvas.h"

namespace astralia::bar {

void paint(ui::Canvas &canvas, const Model &model, const ui::Box *dirty = nullptr);

} // namespace astralia::bar
