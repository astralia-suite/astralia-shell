#pragma once

#include <array>
#include <chrono>
#include <functional>
#include <string>

#include "config/logout_config.h"

#include "core/animation.h"
#include "core/input.h"

namespace astralia {

class LogoutModel;

class LogoutModel {
  public:
    static constexpr int count = logout_config::button_count;
    using Commands = std::array<std::string, count>;
    using Floats = std::array<float, count>;

    explicit LogoutModel(Commands commands);

    void open();
    void toggle();
    void fast_hide();
    void tick(std::chrono::steady_clock::time_point now);

    bool key(const input::KeyEvent &event);
    void click(double x, double y, double width, double height);
    bool hover(double x, double y, double width, double height);
    bool clear_hover();

    bool open_state() const { return open_; }
    bool exiting() const { return exiting_; }
    bool input_ready() const { return input_ready_; }
    bool animating() const { return animations_.hasActive(); }
    int selected() const { return selected_; }
    int hovered() const { return hovered_; }
    float logo_scale() const { return logo_scale_; }
    float exit_fade() const { return exit_fade_; }
    float burst() const { return burst_; }
    const Floats &slash() const { return slash_; }
    const Floats &travel() const { return travel_; }
    const Floats &highlight_scale() const { return highlight_scale_; }
    const Floats &highlight_border() const { return highlight_border_; }
    std::chrono::steady_clock::time_point burst_started() const { return burst_started_; }

    std::function<void(const std::string &)> on_execute;
    std::function<void()> on_closed;

  private:
    void set_highlight(int index, bool on);
    void update_highlight(int index);
    void select(int index);
    void execute(int index);
    void cancel_sequence();
    void start_open();
    void start_close();
    void start_slashes();
    void start_burst();
    void start_exit_burst();
    void finish_close();
    void schedule_after(float delay_ms, uint64_t owner, std::function<void()> fn);

    Commands commands_;
    bool open_ = false;
    bool exiting_ = false;
    bool input_ready_ = false;
    int selected_ = 0;
    int hovered_ = -1;
    float logo_scale_ = 0.0f;
    float exit_fade_ = 0.0f;
    float burst_ = 0.0f;
    std::chrono::steady_clock::time_point burst_started_{};
    Floats slash_{};
    Floats travel_{};
    Floats highlight_scale_{};
    Floats highlight_border_{};
    AnimationManager animations_;
};

std::array<std::string, logout_config::button_count> logout_commands(bool with_lock);

} // namespace astralia
