#pragma once

#include <functional>
#include <string>

#include "modules/settings/model.h"

#include "ui/canvas.h"

namespace astralia {

struct SettingsArt {
    std::string user_name;
    std::string uptime;
    std::function<bool(ui::Canvas &, const ui::Box &)> avatar;
};

struct SettingsFrame {
    ui::Box caret;
    ui::Box panel;
};

SettingsFrame paint_settings(ui::Canvas &canvas, SettingsModel &model, const SettingsArt &art, float width, float height);

} // namespace astralia
