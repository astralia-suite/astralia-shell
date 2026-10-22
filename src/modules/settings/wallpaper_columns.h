#pragma once

#include <string>
#include <vector>

#include "modules/settings/model.h"
#include "modules/settings/pane.h"

#include "service/wallpaper_service.h"

namespace astralia {

struct WallpaperChip {
    int monitor;
    int column;
    std::string label;
};

std::vector<WallpaperChip> wallpaper_chips(const SettingsModel &model, WallpaperSlot slot);

float wallpaper_columns_panel(settings_view::Pane &pane, WallpaperSlot slot, float x, float y,
                              float w, float bottom);

} // namespace astralia
