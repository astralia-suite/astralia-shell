#pragma once

#include <cairo.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <xcb/xcb.h>

#include "app/services.h"

#include "app/shell.h"
#include "core/event_loop.h"
#include "core/x_connection.h"

#include "modules/bar/model.h"
#include "modules/bar/x11/panel_host.h"

#include "render/cairo_canvas.h"
#include "render/x_window.h"

namespace astralia {

class Bar {
  public:
    Bar(XConnection &x, EventLoop &loop, Shell &shell, Services &services, const Output &output);
    ~Bar();
    Bar(const Bar &) = delete;
    Bar &operator=(const Bar &) = delete;

    void place(const Output &output);
    void hide();
    const std::string &output_name() const { return output_name_; }
    bool panels_open() const { return panels_->is_open(); }
    void toggle_panel(PanelId id);

  private:
    bool refresh_style();
    bool refresh_autohide();
    uint16_t collapsed_height() const;
    void set_collapsed(bool collapsed);
    void update_autohide();
    void close_other_overlays(BarItem keep);
    void scroll(const xcb_button_press_event_t &event);
    void apply_output(const OutputGeometry &output);
    void set_hints(const OutputGeometry &output);
    void draw(const ui::Box *dirty = nullptr);
    void prepare();
    void redraw_clock();
    void click(const xcb_button_press_event_t &event);
    void hover(std::optional<int> x, std::optional<int> y);
    void sync_panels();
    void start_linger();
    void sync_hover();
    void refresh_clock();
    std::chrono::milliseconds until_linger_end() const;

    XConnection &x_;
    EventLoop &loop_;
    Shell &shell_;
    Services &services_;
    uint16_t width_ = 0;
    uint16_t height_ = 0;
    uint16_t full_height_ = 0;
    OutputGeometry output_{0, 0, 1, 1};
    bool autohide_ = false;
    bool collapsed_ = false;
    std::string output_name_;
    BarStyle style_ = BarStyle::continuous;
    const BarStyleSpec *spec_ = nullptr;
    XWindow window_;
    CairoCanvas canvas_;
    BarModel model_;
    std::unique_ptr<PanelHost> panels_;
    std::string clock_label_;
    float clock_width_ = 0.0f;
    int linger_timer_ = -1;
};

} // namespace astralia
