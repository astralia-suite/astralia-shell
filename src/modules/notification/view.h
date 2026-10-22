#pragma once

#include "modules/notification/model.h"

#include "render/canvas.h"

namespace astralia::notification {

void paint(ui::Canvas &canvas, const ViewState &view, const Layout &layout);

} // namespace astralia::notification
