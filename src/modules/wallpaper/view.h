#pragma once

#include "modules/wallpaper/model.h"

#include "render/canvas.h"

namespace astralia {

void paint_desktop_menu(ui::Canvas &canvas, const DesktopMenuModel &model, float x, float y);

} // namespace astralia
