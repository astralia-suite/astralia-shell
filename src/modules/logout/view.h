#pragma once

#include "modules/logout/model.h"

#include "render/canvas.h"

namespace astralia {

void paint_logout(ui::Canvas &canvas, const LogoutModel &model, float width, float height);

} // namespace astralia
