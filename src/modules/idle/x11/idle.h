#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <xcb/xcb.h>

#include "app/services.h"

#include "core/event_loop.h"
#include "core/x_connection.h"

#include "modules/idle/bounce.h"
#include "modules/idle/idle_clock.h"

#include "render/cairo_canvas.h"
#include "render/x_window.h"

namespace astralia {

class Idle {
  public:
    Idle(XConnection &x, EventLoop &loop, Services &services);

  private:
    struct Overlay {
        Overlay(XConnection &x) : window(x, "astralia-idle", XCB_EVENT_MASK_EXPOSURE) {}

        XWindow window;
        CairoCanvas canvas;
        IdleVerdict shown;
        Bounce bounce;
    };

    void poll();
    void step();
    void apply(const Output &output, Overlay &overlay, IdleVerdict verdict);
    void hide_missing();
    void paint(const Output &output, Overlay &overlay);
    std::chrono::milliseconds until_step() const;
    bool animating() const;
    Overlay &overlay_for(const Output &output);

    XConnection &x_;
    EventLoop &loop_;
    Services &services_;
    IdleClock clock_;
    Config applied_;
    std::map<std::string, std::unique_ptr<Overlay>> overlays_;
    std::chrono::steady_clock::time_point last_poll_{};
    std::chrono::steady_clock::time_point last_frame_{};
    int timer_ = 0;
    bool available_ = false;
};

} // namespace astralia
