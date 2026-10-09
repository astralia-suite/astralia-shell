#pragma once

#include <xcb/xcb.h>

#include "app/shell.h"
#include "core/event_loop.h"
#include "core/keyboard.h"
#include "core/x_connection.h"

#include "modules/launcher/model.h"
#include "modules/launcher/view.h"

#include "render/cairo_canvas.h"
#include "render/x_window.h"

namespace astralia {

class Launcher {
  public:
    Launcher(XConnection &x, EventLoop &loop, Shell &shell);

  private:
    void toggle(bool global);
    void open(bool global);
    void close();
    void handle(const xcb_generic_event_t &event);
    int row_at(double x, double y) const;
    void paint();

    XConnection &x_;
    XWindow window_;
    Keyboard keyboard_;
    CairoCanvas canvas_;
    LauncherModel model_;
    LauncherFrame frame_;
};

} // namespace astralia
