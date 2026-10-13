#pragma once

#include <functional>
#include <string>
#include <xcb/xcb.h>

#include "core/clipboard.h"
#include "core/x_connection.h"

namespace astralia {

class X11Clipboard final : public Clipboard {
  public:
    X11Clipboard(XConnection &x, xcb_window_t window);

    void set(std::string text) override;
    void request(std::function<void(std::string)> done) override;

    bool handle(const xcb_generic_event_t &event);

  private:
    void answer(const xcb_selection_request_event_t &request);
    void receive(const xcb_selection_notify_event_t &notify);

    XConnection &x_;
    xcb_window_t window_;
    std::string text_;
    bool owner_ = false;
    std::function<void(std::string)> pending_;
};

} // namespace astralia
