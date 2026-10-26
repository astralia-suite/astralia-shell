#pragma once

#include <optional>

#include "modules/bar/model/model.h"
#include "modules/bar/panel/core/panel_set.h"

namespace astralia::bar {

std::optional<PanelId> panel_for(Item item);
std::optional<Item> item_for(PanelId panel);

inline PanelId panel_of(Item item) { return panel_for(item).value_or(PanelId::count); }

} // namespace astralia::bar
