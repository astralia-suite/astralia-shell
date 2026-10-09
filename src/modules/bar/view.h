#pragma once

#include "modules/bar/model.h"

#include "render/canvas.h"

namespace astralia {

void paint_bar(ui::Canvas &canvas, const BarModel &model, const ui::Box *dirty = nullptr);

} // namespace astralia
