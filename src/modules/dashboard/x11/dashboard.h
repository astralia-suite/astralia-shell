#pragma once

#include <xcb/xcb.h>

#include "app/active_target.h"
#include "app/services.h"
#include "app/shell.h"
#include "core/event_loop.h"
#include "core/keyboard.h"
#include "core/x_connection.h"

#include "render/cairo_canvas.h"
#include "render/x_window.h"

namespace astralia {

class Dashboard {
  public:
    Dashboard(XConnection &x, EventLoop &loop, Shell &shell, Services &services);

  private:
    void toggle();
    void open();
    void close();
    void handle(const xcb_generic_event_t &event);
    void paint();

    XConnection &x_;
    Services &services_;
    XWindow window_;
    Keyboard keyboard_;
    CairoCanvas canvas_;
    bool open_ = false;
};

} // namespace astralia
