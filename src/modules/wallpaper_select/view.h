#pragma once

#include "modules/wallpaper_select/model.h"

#include "render/canvas.h"

namespace astralia::wallpaper_select {

void paint(ui::Canvas &canvas, Model &model, float width, float height);

} // namespace astralia::wallpaper_select
