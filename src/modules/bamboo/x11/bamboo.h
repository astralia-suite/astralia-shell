#pragma once

#include <chrono>
#include <xcb/xcb.h>

#include "app/active_target.h"
#include "app/services.h"
#include "app/shell.h"

#include "core/event_loop.h"
#include "core/keyboard.h"
#include "core/x_connection.h"

#include "config/bamboo_config.h"

#include "modules/bamboo/bamboo_grow.h"

#include "render/cairo_canvas.h"
#include "render/x_window.h"

namespace astralia {

class Bamboo {
  public:
    Bamboo(XConnection &x, EventLoop &loop, Shell &shell, Services &services);

  private:
    void toggle();
    void open();
    void close();
    void rebuild();
    void step();
    void handle(const xcb_generic_event_t &event);
    void paint();

    XConnection &x_;
    EventLoop &loop_;
    Services &services_;
    XWindow window_;
    Keyboard keyboard_;
    CairoCanvas canvas_;
    BambooGrow grow_;
    int timer_ = 0;
    bool open_ = false;
};

} // namespace astralia
