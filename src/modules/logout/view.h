#pragma once

#include <functional>

#include "modules/logout/model.h"

#include "ui/canvas.h"

namespace astralia {

using LogoutLogoPainter = std::function<void(ui::Canvas &, const ui::Box &, float alpha)>;

void paint_logout(ui::Canvas &canvas, const LogoutModel &model, float width, float height, const LogoutLogoPainter &logo);

} // namespace astralia
