#pragma once

#include "modules/wallpaper_select/model.h"

#include "render/canvas.h"

namespace astralia {

void paint_wallpaper_select(ui::Canvas &canvas, WallpaperSelectModel &model, float width, float height);

} // namespace astralia
