#pragma once

#include <utility>
#include <vector>

#include "modules/launcher/entrance.h"
#include "modules/launcher/model.h"

#include "render/canvas.h"

namespace astralia::launcher {

struct Frame {
    ui::Box box;
    ui::Box caret;
    std::vector<std::pair<ui::Box, int>> rows;
    EntranceFrame entrance;
    ui::Box field;
    float text_x = 0.0f;
    float cell_w = 0.0f;
    size_t first_column = 0;
};

bool field_hit(const Frame &frame, double x, double y);

float field_column(const Frame &frame, double x);

Frame paint(ui::Canvas &canvas, Model &model, float width, float height,
            const EntranceState *entrance = nullptr);

} // namespace astralia::launcher
