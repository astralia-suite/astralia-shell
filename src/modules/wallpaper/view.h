#pragma once

#include "modules/wallpaper/model.h"

#include "render/canvas.h"

namespace astralia::wallpaper {

void paint(ui::Canvas &canvas, const DesktopMenuModel &model, float x, float y);

} // namespace astralia::wallpaper
