#pragma once

#include "modules/logout/model.h"

#include <string_view>

#include "render/canvas.h"

namespace astralia {

void paint_logout(ui::Canvas &canvas, const LogoutModel &model, float width, float height,
                  std::string_view logo_path);

} // namespace astralia
