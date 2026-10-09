#pragma once

#include <chrono>
#include <xcb/xcb.h>

#include "app/services.h"

#include "app/shell.h"
#include "core/event_loop.h"
#include "core/keyboard.h"
#include "core/x_connection.h"

#include "modules/overview/model.h"

#include "render/cairo_canvas.h"
#include "render/x_window.h"

namespace astralia {

class Overview {
  public:
    Overview(XConnection &x, EventLoop &loop, Shell &shell, Services &services);

  private:
    void toggle();
    void open();
    void closed();
    void handle(const xcb_generic_event_t &event);
    void expect_focus_loss();
    void paint();

    XConnection &x_;
    Services &services_;
    XWindow window_;
    Keyboard keyboard_;
    CairoCanvas canvas_;
    OverviewModel model_;
    std::chrono::steady_clock::time_point focus_grace_until_{};
};

} // namespace astralia
