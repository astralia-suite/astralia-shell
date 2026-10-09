#pragma once

#include <memory>
#include <xcb/xcb.h>

#include "app/services.h"

#include "core/event_loop.h"
#include "core/keyboard.h"
#include "core/signal.h"
#include "core/x_connection.h"

#include "modules/bar/panel/panel_set.h"

#include "render/cairo_canvas.h"
#include "render/x_window.h"

namespace astralia {

class PanelHost {
  public:
    PanelHost(XConnection &x, EventLoop &loop, Services &services);
    PanelHost(const PanelHost &) = delete;
    PanelHost &operator=(const PanelHost &) = delete;

    PanelSet &panels() { return set_; }
    void set_output(const OutputGeometry &output, int top);
    bool is_open() const { return set_.any_open(); }

    Signal<> changed;

  private:
    void handle(const xcb_generic_event_t &event);
    void state_changed(PanelId id, bool open);
    void paint();
    void grab();
    void ungrab();
    void sync_popup();
    void hide_popup();
    void handle_popup(const xcb_generic_event_t &event);

    XConnection &x_;
    Keyboard keyboard_;
    PanelSet set_;
    XWindow window_;
    CairoCanvas canvas_;
    XWindow popup_;
    CairoCanvas popup_canvas_;
    OutputGeometry output_;
    int top_ = 0;
    bool grabbed_ = false;
};

} // namespace astralia
