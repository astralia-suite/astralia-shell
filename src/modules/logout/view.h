#pragma once

#include <string_view>

#include "modules/logout/model.h"

#include "render/canvas.h"

namespace astralia::logout {

void paint(ui::Canvas &canvas, const Model &model, float width, float height,
           std::string_view logo_path);

} // namespace astralia::logout
