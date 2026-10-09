#pragma once

#include <functional>

#include "modules/overview/model.h"

#include "render/canvas.h"

namespace astralia {

using OverviewTileArt = std::function<bool(ui::Canvas &, const OverviewTile &, const ui::Box &, float radius)>;

void paint_overview(ui::Canvas &canvas, OverviewModel &model, const OverviewTileArt &art);

} // namespace astralia
