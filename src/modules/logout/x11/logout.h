#pragma once

#include <xcb/xcb.h>

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
    Logout(XConnection &x, EventLoop &loop, Shell &shell);

  private:
    void open();
    void handle(const xcb_generic_event_t &event);
    void paint();
    void closed();

    XConnection &x_;
    XWindow window_;
    Keyboard keyboard_;
    CairoCanvas canvas_;
    LogoutModel model_;
};

} // namespace astralia
