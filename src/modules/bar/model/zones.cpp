#include "modules/bar/model/zones.h"

namespace astralia::bar {

const Zones &classic_zones() {
    static const Zones zones{
        {{SlotKind::item, Item::logout}, {SlotKind::workspaces}, {SlotKind::dock}},
        {{SlotKind::item, Item::media}, {SlotKind::item, Item::time}, {SlotKind::item, Item::date}},
        {{SlotKind::item, Item::tray},
         {SlotKind::item, Item::resource},
         {SlotKind::item, Item::network},
         {SlotKind::item, Item::bluetooth},
         {SlotKind::item, Item::volume},
         {SlotKind::item, Item::brightness},
         {SlotKind::item, Item::battery}},
    };
    return zones;
}

const Zones &pop_zones() {
    static const Zones zones{
        {{SlotKind::item, Item::active_window},
         {SlotKind::item, Item::media},
         {SlotKind::item, Item::tray}},
        {{SlotKind::status},
         {SlotKind::workspaces},
         {SlotKind::item, Item::time},
         {SlotKind::item, Item::date}},
        {{SlotKind::item, Item::resource},
         {SlotKind::item, Item::brightness},
         {SlotKind::item, Item::battery},
         {SlotKind::item, Item::notifications}},
    };
    return zones;
}

} // namespace astralia::bar
