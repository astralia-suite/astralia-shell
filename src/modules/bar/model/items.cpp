#include "modules/bar/model/items.h"

namespace astralia::bar {

std::optional<PanelId> panel_for(Item item) {
    switch (item) {
    case Item::tray:
        return PanelId::tray;
    case Item::resource:
        return PanelId::resource;
    case Item::network:
        return PanelId::network;
    case Item::bluetooth:
        return PanelId::bluetooth;
    case Item::volume:
        return PanelId::volume;
    case Item::battery:
        return PanelId::battery;
    case Item::media:
        return PanelId::media;
    case Item::brightness:
        return PanelId::brightness;
    case Item::time:
        return PanelId::time;
    case Item::date:
        return PanelId::date;
    case Item::logout:
    case Item::active_window:
    case Item::notifications:
    case Item::count:
        break;
    }
    return std::nullopt;
}

std::optional<Item> item_for(PanelId panel) {
    for (size_t i = 0; i < item_count; ++i) {
        if (panel_for(static_cast<Item>(i)) == panel) {
            return static_cast<Item>(i);
        }
    }
    return std::nullopt;
}

} // namespace astralia::bar
