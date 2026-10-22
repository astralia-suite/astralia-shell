#pragma once

#include <vector>
#include <xcb/xcb.h>

#include "app/active_target.h"
#include "app/services.h"
#include "app/shell.h"

#include "core/event_loop.h"
#include "core/keyboard.h"
#include "core/x_connection.h"

#include "modules/visualizer/audio_capture.h"
#include "modules/visualizer/cpu_stages.h"

#include "render/cairo_canvas.h"
#include "render/x_window.h"

namespace astralia {

class Visualizer {
  public:
    Visualizer(XConnection &x, EventLoop &loop, Shell &shell, Services &services);
    ~Visualizer();
    Visualizer(const Visualizer &) = delete;
    Visualizer &operator=(const Visualizer &) = delete;

  private:
    void toggle();
    void open();
    void close();
    void step();
    void handle(const xcb_generic_event_t &event);
    void paint();
    int fps() const;

    XConnection &x_;
    EventLoop &loop_;
    Services &services_;
    XWindow window_;
    Keyboard keyboard_;
    CairoCanvas canvas_;
    VisualizerAudioCapture capture_;
    VisualizerCpuStages stages_;
    std::vector<float> left_;
    std::vector<float> right_;
    int audio_size_ = 0;
    int timer_ = 0;
    bool open_ = false;
};

} // namespace astralia
