#pragma once

#include <chrono>
#include <xcb/xcb.h>

#include "app/active_target.h"
#include "app/services.h"
#include "app/shell.h"

#include "core/event_loop.h"
#include "core/keyboard.h"
#include "core/x_connection.h"

#include "config/rain_config.h"

#include "modules/rain/matrix_rain.h"
#include "modules/rain/stiletto_rain.h"

#include "render/cairo_canvas.h"
#include "render/x_window.h"

namespace astralia {

class Rain {
  public:
    Rain(XConnection &x, EventLoop &loop, Shell &shell, Services &services);

  private:
    void toggle();
    void open();
    void close();
    void apply_params(const RainParams &params);
    void rebuild();
    void step();
    void handle(const xcb_generic_event_t &event);
    void paint();
    RainFrame frame() const;

    XConnection &x_;
    EventLoop &loop_;
    Services &services_;
    XWindow window_;
    Keyboard keyboard_;
    CairoCanvas canvas_;
    MatrixRain matrix_;
    StilettoRain stiletto_;
    RainParams params_;
    int timer_ = 0;
    bool open_ = false;
};

} // namespace astralia
