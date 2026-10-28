#pragma once

#include <array>
#include <vector>

#include "modules/bar/model/item.h"

namespace astralia::bar {

enum class SlotKind { item, workspaces, status, dock };

struct Slot {
    SlotKind kind;
    Item item = Item::count;
};

struct Zones {
    std::vector<Slot> left;
    std::vector<Slot> center;
    std::vector<Slot> right;
};

inline constexpr std::array<Item, 3> status_members{Item::network, Item::bluetooth, Item::volume};

const Zones &bar_zones();

} // namespace astralia::bar
