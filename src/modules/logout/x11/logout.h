#pragma once

#include <xcb/xcb.h>

#include "app/active_target.h"
#include "app/services.h"
#include "app/shell.h"
#include "core/event_loop.h"
#include "core/keyboard.h"
#include "core/x_connection.h"

#include "modules/logout/model.h"

#include "render/cairo_canvas.h"
#include "render/x_window.h"

namespace astralia {

class Logout {
  public:
    Logout(XConnection &x, EventLoop &loop, Shell &shell, Services &services);

  private:
    void open();
    void handle(const xcb_generic_event_t &event);
    void paint();
    void closed();

    XConnection &x_;
    Services &services_;
    XWindow window_;
    Keyboard keyboard_;
    CairoCanvas canvas_;
    LogoutModel model_;
    std::string bound_output_;
};

} // namespace astralia
