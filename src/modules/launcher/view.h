#pragma once

#include <utility>
#include <vector>

#include "modules/launcher/entrance.h"
#include "modules/launcher/model.h"

#include "render/canvas.h"

namespace astralia {

struct LauncherFrame {
    ui::Box box;
    ui::Box caret;
    std::vector<std::pair<ui::Box, int>> rows;
    EntranceFrame entrance;
    ui::Box field;
    float text_x = 0.0f;
    float cell_w = 0.0f;
    size_t first_column = 0;
};

bool launcher_field_hit(const LauncherFrame &frame, double x, double y);

float launcher_field_column(const LauncherFrame &frame, double x);

LauncherFrame paint_launcher(ui::Canvas &canvas, LauncherModel &model, float width, float height,
                             const EntranceState *entrance = nullptr);

} // namespace astralia
