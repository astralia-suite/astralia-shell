#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "service/settings_service.h"

namespace astralia::idle {

class Clock {
  public:
    using Time = std::chrono::steady_clock::time_point;

    void reset(const std::vector<std::string> &outputs, Time now);
    void activity(const std::string &output, Time now);
    bool idle(const std::string &output, uint32_t timeout_seconds, Time now) const;
    bool known(const std::string &output) const { return last_activity_.contains(output); }

  private:
    std::map<std::string, Time> last_activity_;
};

struct Verdict {
    bool ambient = false;
    bool screensaver = false;

    bool operator==(const Verdict &) const = default;
};

Verdict idle_verdict(const Clock &clock, const Config &config, const std::string &output,
                     Clock::Time now);
bool policy_changed(const Config &before, const Config &after);

} // namespace astralia::idle
