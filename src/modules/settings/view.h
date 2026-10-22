#pragma once

#include <string>

#include "modules/settings/model.h"

#include "render/canvas.h"

namespace astralia::settings {

struct Art {
    std::string user_name;
    std::string uptime;
    std::string face;
};

struct Frame {
    ui::Box caret;
    ui::Box panel;
};

Frame paint(ui::Canvas &canvas, Model &model, const Art &art, float width, float height);

} // namespace astralia::settings
