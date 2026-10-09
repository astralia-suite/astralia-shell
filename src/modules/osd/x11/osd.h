#pragma once

#include <xcb/xcb.h>

#include "app/services.h"

#include "core/event_loop.h"
#include "core/x_connection.h"

#include "modules/osd/model.h"

#include "render/cairo_canvas.h"
#include "render/x_window.h"

namespace astralia {

class Osd {
  public:
    Osd(XConnection &x, EventLoop &loop, Services &services);

  private:
    void show(OsdKind kind, int percent, bool muted);
    void hide();
    std::chrono::milliseconds until_hide() const;
    void paint();

    XConnection &x_;
    EventLoop &loop_;
    Services &services_;
    XWindow window_;
    CairoCanvas canvas_;
    OsdModel model_;
    int timer_;
};

} // namespace astralia
