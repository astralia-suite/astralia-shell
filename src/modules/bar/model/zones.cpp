#include "modules/bar/model/zones.h"

namespace astralia::bar {

const Zones &bar_zones() {
    static const Zones zones{
        {{SlotKind::item, Item::logout},
         {SlotKind::item, Item::active_window},
         {SlotKind::item, Item::media},
         {SlotKind::item, Item::tray}},
        {{SlotKind::status},
         {SlotKind::workspaces},
         {SlotKind::item, Item::time},
         {SlotKind::item, Item::date}},
        {{SlotKind::dock},
         {SlotKind::item, Item::resource},
         {SlotKind::item, Item::brightness},
         {SlotKind::item, Item::battery},
         {SlotKind::item, Item::notifications}},
    };
    return zones;
}

} // namespace astralia::bar
