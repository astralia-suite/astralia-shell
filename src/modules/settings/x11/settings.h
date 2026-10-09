#pragma once

#include <chrono>
#include <xcb/xcb.h>

#include "app/services.h"

#include "app/shell.h"
#include "core/event_loop.h"
#include "core/keyboard.h"
#include "core/x_connection.h"

#include "modules/settings/model.h"
#include "modules/settings/view.h"

#include "render/cairo_canvas.h"
#include "render/x_window.h"

namespace astralia {

class Settings {
  public:
    Settings(XConnection &x, EventLoop &loop, Shell &shell, Services &services);

  private:
    void toggle();
    void open();
    void close();
    void handle(const xcb_generic_event_t &event);
    void schedule_paint();
    void paint();

    XConnection &x_;
    EventLoop &loop_;
    Services &services_;
    XWindow window_;
    Keyboard keyboard_;
    CairoCanvas canvas_;
    SettingsModel model_;
    bool open_ = false;
    bool repaint_pending_ = false;
    std::chrono::steady_clock::time_point refocus_until_{};
    int repaint_timer_ = 0;
};

} // namespace astralia
