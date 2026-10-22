#pragma once

#include <functional>
#include <string>
#include <vector>
#include <xcb/xcb.h>

#include "app/services.h"

#include "core/event_loop.h"
#include "core/x_connection.h"

#include "modules/wallpaper/model.h"

#include "render/cairo_canvas.h"
#include "render/x_window.h"

namespace astralia::wallpaper {

class Wallpaper {
  public:
    Wallpaper(XConnection &x, EventLoop &loop, Services &services);
    ~Wallpaper();
    Wallpaper(const Wallpaper &) = delete;
    Wallpaper &operator=(const Wallpaper &) = delete;

    std::function<void()> on_settings;
    std::function<void(const std::string &, int)> on_wallpaper;

  private:
    void apply(bool force);
    void layout_desktop_windows();
    void handle_desktop(size_t index, const xcb_generic_event_t &event);
    void handle_menu(const xcb_generic_event_t &event);
    void open_menu(size_t index, int16_t root_x, int16_t root_y, int16_t event_x);
    void close_menu();
    void paint_menu();
    void grab_menu();

    XConnection &x_;
    EventLoop &loop_;
    Services &services_;
    std::vector<Output> applied_;
    std::string applied_signature_;
    xcb_pixmap_t pixmap_ = XCB_NONE;
    std::vector<xcb_window_t> desktop_windows_;
    std::vector<std::string> desktop_outputs_;
    XWindow menu_window_;
    CairoCanvas menu_canvas_;
    DesktopMenuModel menu_;
    bool grabbed_ = false;
    size_t menu_output_ = 0;
};

} // namespace astralia::wallpaper
