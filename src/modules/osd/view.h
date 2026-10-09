#pragma once

#include "modules/osd/model.h"

#include "render/canvas.h"

namespace astralia {

void paint_osd(ui::Canvas &canvas, const OsdModel &model);

} // namespace astralia
