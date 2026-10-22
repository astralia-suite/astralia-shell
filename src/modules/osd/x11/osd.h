#pragma once

#include <map>
#include <memory>
#include <string>
#include <xcb/xcb.h>

#include "app/services.h"

#include "core/event_loop.h"
#include "core/x_connection.h"

#include "modules/osd/model.h"

#include "render/cairo_canvas.h"
#include "render/x_window.h"

namespace astralia::osd {

class Osd {
  public:
    Osd(XConnection &x, EventLoop &loop, Services &services);

  private:
    struct Surface {
        explicit Surface(XConnection &x) : window(x, "astralia-osd", XCB_EVENT_MASK_EXPOSURE) {}

        XWindow window;
        CairoCanvas canvas;
    };

    void show(OsdKind kind, int percent, bool muted);
    void hide();
    std::chrono::milliseconds until_hide() const;
    void paint(Surface &surface);
    Surface &surface_for(const std::string &output);

    XConnection &x_;
    EventLoop &loop_;
    Services &services_;
    Model model_;
    std::map<std::string, std::unique_ptr<Surface>> surfaces_;
    int timer_;
};

} // namespace astralia::osd
