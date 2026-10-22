#pragma once

#include <functional>

#include "modules/overview/model.h"

#include "render/canvas.h"

namespace astralia::overview {

using TileArt = std::function<bool(ui::Canvas &, const Tile &, const ui::Box &, float radius)>;

void paint(ui::Canvas &canvas, Model &model, const TileArt &art);

} // namespace astralia::overview
