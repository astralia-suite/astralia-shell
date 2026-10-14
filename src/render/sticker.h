#pragma once

#include <optional>

#include "render/canvas.h"
#include "render/tokens.h"

namespace astralia {

void sticker(ui::Canvas &canvas, const ui::Box &box, float radius, const Color &fill, const Color &outline = palette::ink, float border_width = metrics::border_thick, float shadow = metrics::shadow_offset, std::optional<Color> shadow_color = std::nullopt, std::optional<float> shadow_dx = std::nullopt);

} // namespace astralia
