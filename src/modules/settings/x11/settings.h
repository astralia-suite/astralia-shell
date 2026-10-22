#pragma once

#include <chrono>
#include <xcb/xcb.h>

#include "app/active_target.h"
#include "app/services.h"
#include "app/shell.h"

#include "core/event_loop.h"
#include "core/keyboard.h"
#include "core/x_connection.h"

#include "modules/settings/model.h"
#include "modules/settings/view.h"

#include "render/cairo_canvas.h"
#include "render/x_window.h"

namespace astralia::settings {

class Settings {
  public:
    Settings(XConnection &x, EventLoop &loop, Shell &shell, Services &services);

  private:
    void toggle();
    void open();
    void close();
    void closed();
    void handle(const xcb_generic_event_t &event);
    void schedule_paint();
    void paint();
    void expect_focus_loss();

    XConnection &x_;
    EventLoop &loop_;
    Services &services_;
    XWindow window_;
    Keyboard keyboard_;
    CairoCanvas canvas_;
    Model model_;
    bool open_ = false;
    bool repaint_pending_ = false;
    int repaint_timer_ = 0;
    int caret_timer_ = 0;
    std::string bound_output_;
    std::chrono::steady_clock::time_point refocus_until_{};
};

} // namespace astralia::settings
