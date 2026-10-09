#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <xcb/xcb.h>

#include "app/services.h"
#include "app/shell.h"

#include "core/event_loop.h"
#include "core/keyboard.h"
#include "core/x_connection.h"

#include "modules/lock/model.h"
#include "modules/lock/view.h"

#include "render/cairo_canvas.h"
#include "render/image_decode.h"
#include "render/x_window.h"

namespace astralia {

class Lock {
  public:
    Lock(XConnection &x, EventLoop &loop, Shell &shell, Services &services);

  private:
    struct Surface {
        Surface(XConnection &x, size_t index);

        XWindow window;
        CairoCanvas canvas;
        Output output;
        LockHits hits;
        SurfacePtr wallpaper;
        std::string wallpaper_key;
    };

    void lock();
    void unlock();
    bool grab();
    void release_grabs();
    void handle(size_t index, const xcb_generic_event_t &event);
    void press(const xcb_key_press_event_t &event);
    void click(const xcb_button_press_event_t &event);
    void submit();
    void finished(uint64_t generation, bool success);
    void poll();
    void paint_all(bool with_wallpaper);
    void paint(Surface &surface, bool with_wallpaper);
    LockInfo info() const;
    std::chrono::milliseconds next_tick() const;

    XConnection &x_;
    EventLoop &loop_;
    Services &services_;
    Keyboard keyboard_;
    LockModel model_;
    std::vector<std::unique_ptr<Surface>> surfaces_;
    CpuTempState cpu_temp_;
    GpuTempState gpu_temp_;
    SystemStatsState stats_;
    std::string user_;
    int timer_ = -1;
    bool locked_ = false;
};

} // namespace astralia
