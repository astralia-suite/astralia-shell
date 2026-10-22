#include "modules/idle/clock.h"

namespace astralia::idle {

void Clock::reset(const std::vector<std::string> &outputs, Time now) {
    for (const std::string &output : outputs) {
        last_activity_[output] = now;
    }
}

void Clock::activity(const std::string &output, Time now) { last_activity_[output] = now; }

bool Clock::idle(const std::string &output, uint32_t timeout_seconds, Time now) const {
    auto it = last_activity_.find(output);
    if (it == last_activity_.end()) {
        return false;
    }
    return std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count() >
           static_cast<int64_t>(timeout_seconds);
}

Verdict idle_verdict(const Clock &clock, const Config &config, const std::string &output,
                     Clock::Time now) {
    Verdict verdict;
    verdict.ambient = ambient_enabled_for(config, output) &&
                      clock.idle(output, ambient_timeout_for(config, output), now);
    verdict.screensaver = screensaver_enabled_for(config, output) &&
                          clock.idle(output, screensaver_timeout_for(config, output), now);
    return verdict;
}

bool policy_changed(const Config &before, const Config &after) {
    return before.idle_enabled != after.idle_enabled ||
           before.ambient_enabled != after.ambient_enabled ||
           before.ambient_timeout != after.ambient_timeout ||
           before.screensaver_enabled != after.screensaver_enabled ||
           before.screensaver_timeout != after.screensaver_timeout ||
           before.monitor_overrides != after.monitor_overrides;
}

} // namespace astralia::idle
