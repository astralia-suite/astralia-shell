#pragma once

#include <xcb/xcb.h>

#include "app/active_target.h"
#include "app/services.h"
#include "app/shell.h"

#include "core/event_loop.h"
#include "core/keyboard.h"
#include "core/x_clipboard.h"
#include "core/x_connection.h"

#include "modules/launcher/model.h"
#include "modules/launcher/view.h"

#include "render/cairo_canvas.h"
#include "render/x_window.h"

namespace astralia::launcher {

class Launcher {
  public:
    Launcher(XConnection &x, EventLoop &loop, Shell &shell, Services &services);

  private:
    void toggle(bool global);
    void open(bool global);
    void close();
    void handle(const xcb_generic_event_t &event);
    int row_at(double x, double y) const;
    void paint();

    XConnection &x_;
    EventLoop &loop_;
    Services &services_;
    XWindow window_;
    Keyboard keyboard_;
    X11Clipboard clipboard_;
    CairoCanvas canvas_;
    Model model_;
    Frame frame_;
    std::string bound_output_;
    int caret_timer_ = 0;
};

} // namespace astralia::launcher
