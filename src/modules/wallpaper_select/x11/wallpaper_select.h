#pragma once

#include <string>
#include <xcb/xcb.h>

#include "app/services.h"
#include "app/shell.h"
#include "core/event_loop.h"
#include "core/keyboard.h"
#include "core/x_connection.h"

#include "modules/wallpaper_select/model.h"

#include "render/cairo_canvas.h"
#include "render/x_window.h"

namespace astralia {

class WallpaperSelect {
  public:
    WallpaperSelect(XConnection &x, EventLoop &loop, Shell &shell, Services &services);

    void open_on(const std::string &monitor, int column);

  private:
    void toggle();
    void close();
    void closed();
    void handle(const xcb_generic_event_t &event);
    void schedule_paint();
    void paint();

    XConnection &x_;
    EventLoop &loop_;
    Services &services_;
    XWindow window_;
    Keyboard keyboard_;
    CairoCanvas canvas_;
    WallpaperSelectModel model_;
    bool open_ = false;
    bool repaint_pending_ = false;
    int repaint_timer_ = 0;
    std::string bound_output_;
};

} // namespace astralia
