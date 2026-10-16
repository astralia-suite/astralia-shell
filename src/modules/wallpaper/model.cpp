#include <algorithm>

#include "config/wallpaper_config.h"

#include "modules/wallpaper/model.h"

#include "render/icons.h"
#include "render/tokens.h"

namespace astralia {

namespace {

namespace cfg = wallpaper_config;

}

const std::vector<DesktopMenuEntry> &DesktopMenuModel::entries() {
    static const std::vector<DesktopMenuEntry> list{
        {DesktopMenuAction::settings, "Settings", icon::settings},
        {DesktopMenuAction::wallpaper, "Wallpaper", icon::palette_icon},
    };
    return list;
}

void DesktopMenuModel::open(float x, float y, float bounds_width, float bounds_height, int column) {
    float count = static_cast<float>(entries().size());
    float width = cfg::menu_width;
    float height = cfg::menu_padding * 2.0f + count * cfg::menu_row_height +
                   (count - 1.0f) * cfg::menu_row_gap + metrics::shadow_offset;
    box_ = {std::clamp(x, 0.0f, std::max(0.0f, bounds_width - width)),
            std::clamp(y, 0.0f, std::max(0.0f, bounds_height - height)), width, height};
    column_ = column;
    hovered_ = -1;
    open_ = true;
}

void DesktopMenuModel::close() {
    open_ = false;
    hovered_ = -1;
}

bool DesktopMenuModel::toggle(float x, float y, float bounds_width, float bounds_height,
                              int column) {
    if (open_) {
        close();
    } else {
        open(x, y, bounds_width, bounds_height, column);
    }
    return open_;
}

ui::Box DesktopMenuModel::row(int index) const {
    float top =
        cfg::menu_padding + static_cast<float>(index) * (cfg::menu_row_height + cfg::menu_row_gap);
    return {cfg::menu_padding, top,
            cfg::menu_width - cfg::menu_padding * 2.0f - metrics::shadow_offset,
            cfg::menu_row_height};
}

int DesktopMenuModel::entry_at(float local_x, float local_y) const {
    if (!open_) {
        return -1;
    }
    for (int i = 0; i < static_cast<int>(entries().size()); ++i) {
        ui::Box box = row(i);
        if (local_x >= box.x && local_x < box.x + box.w && local_y >= box.y &&
            local_y < box.y + box.h) {
            return i;
        }
    }
    return -1;
}

bool DesktopMenuModel::hover(float local_x, float local_y) {
    int index = entry_at(local_x, local_y);
    if (index == hovered_) {
        return false;
    }
    hovered_ = index;
    return true;
}

void DesktopMenuModel::clear_hover() { hovered_ = -1; }

} // namespace astralia
