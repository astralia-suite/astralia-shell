#pragma once

#include <map>
#include <memory>
#include <string>
#include <xcb/xcb.h>

#include "app/services.h"

#include "core/event_loop.h"
#include "core/x_connection.h"

#include "modules/notification/model.h"

#include "render/cairo_canvas.h"
#include "render/x_window.h"

namespace astralia::notification {

class Host {
  public:
    Host(XConnection &x, EventLoop &loop, Services &services);

  private:
    struct View {
        explicit View(XConnection &x);

        XWindow window;
        CairoCanvas canvas;
        ViewState state;
        Layout layout;
        Boxes close_boxes;
        Boxes card_boxes;
        Boxes input_boxes;
    };

    void sync();
    View &view_for(const Output &output);
    void handle(View &view, const xcb_generic_event_t &event);
    void paint(View &view);
    void apply_input_region(View &view);

    XConnection &x_;
    EventLoop &loop_;
    Services &services_;
    Model model_;
    NotificationService &service_;
    std::map<std::string, std::unique_ptr<View>> views_;
};

} // namespace astralia::notification
