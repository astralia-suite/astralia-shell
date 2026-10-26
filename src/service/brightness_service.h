#pragma once

#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/async_process.h"
#include "core/reactor.h"
#include "core/signal.h"
#include "core/unique_fd.h"

namespace astralia {

struct DdcBus {
    std::string output;
    std::string bus;
};

std::vector<DdcBus> parse_ddc_detect(std::string_view text);
std::optional<std::pair<int, int>> parse_ddc_brightness(std::string_view text);

// Brightness for the laptop panel (sysfs backlight) and for external monitors (ddcutil, DDC/CI).
// An output with a DDC display uses it; any other output falls back to the laptop panel.
class BrightnessService {
  public:
    explicit BrightnessService(Reactor &loop);
    ~BrightnessService();
    BrightnessService(const BrightnessService &) = delete;
    BrightnessService &operator=(const BrightnessService &) = delete;

    bool available() const { return max_ > 0; }
    int percent() const;
    void set(int percent) const;

    bool available(const std::string &output) const;
    int percent(const std::string &output) const;
    void set(const std::string &output, int percent);
    void refresh(const std::string &output);

    Signal<> changed;

  private:
    struct Display {
        DdcBus ddc;
        int max = 0;
        int value = 0;
    };
    struct Job {
        std::string output;
        bool write = false;
        int raw = 0;
    };

    Display *find(const std::string &output);
    const Display *find(const std::string &output) const;
    void queue(Job job);
    void next();
    void detected(const std::string &text);

    Reactor &loop_;
    std::string device_;
    int max_ = 0;
    UniqueFd watch_;
    AsyncProcess detect_;
    AsyncProcess worker_;
    std::vector<Display> displays_;
    std::deque<Job> jobs_;
};

} // namespace astralia
