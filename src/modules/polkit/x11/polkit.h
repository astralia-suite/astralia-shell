#pragma once

#include <xcb/xcb.h>

#include "app/services.h"

#include "core/event_loop.h"
#include "core/keyboard.h"
#include "core/x_connection.h"

#include "modules/polkit/model.h"

#include "render/cairo_canvas.h"
#include "render/x_window.h"

namespace astralia {

class Polkit {
  public:
    Polkit(XConnection &x, EventLoop &loop, Services &services);

  private:
    void sync();
    void open();
    void handle(const xcb_generic_event_t &event);
    void paint();

    XConnection &x_;
    XWindow window_;
    Keyboard keyboard_;
    CairoCanvas canvas_;
    PolkitModel model_;
    PolkitService &service_;
};

} // namespace astralia
