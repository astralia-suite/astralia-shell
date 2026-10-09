#pragma once

#include "modules/notification/model.h"

#include "render/canvas.h"

namespace astralia {

void paint_notifications(ui::Canvas &canvas, const NotificationViewState &view, const NotificationLayout &layout);

} // namespace astralia
