#pragma once

#include <xcb/xcb.h>

#include "app/services.h"

#include "core/event_loop.h"
#include "core/x_connection.h"

#include "modules/notification/model.h"

#include "render/cairo_canvas.h"
#include "render/x_window.h"

namespace astralia {

class Notifications {
  public:
    Notifications(XConnection &x, EventLoop &loop, Services &services);

  private:
    void sync();
    void handle(const xcb_generic_event_t &event);
    void paint();

    Services &services_;
    XWindow window_;
    CairoCanvas canvas_;
    NotificationModel model_;
    NotificationViewState view_;
    NotificationLayout layout_;
    NotificationService &service_;
};

} // namespace astralia
