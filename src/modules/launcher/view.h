#pragma once

#include <utility>
#include <vector>

#include "modules/launcher/model.h"

#include "render/canvas.h"

namespace astralia {

struct LauncherFrame {
    ui::Box box;
    ui::Box caret;
    std::vector<std::pair<ui::Box, int>> rows;
};

LauncherFrame paint_launcher(ui::Canvas &canvas, LauncherModel &model, float width, float height);

} // namespace astralia
