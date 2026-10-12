#include "modules/idle/idle_clock.h"

namespace astralia {

void IdleClock::reset(const std::vector<std::string> &outputs, Time now) {
    for (const std::string &output : outputs) {
        last_activity_[output] = now;
    }
}

void IdleClock::activity(const std::string &output, Time now) { last_activity_[output] = now; }

bool IdleClock::idle(const std::string &output, uint32_t timeout_seconds, Time now) const {
    auto it = last_activity_.find(output);
    if (it == last_activity_.end()) {
        return false;
    }
    return std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count() > static_cast<int64_t>(timeout_seconds);
}

IdleVerdict idle_verdict(const IdleClock &clock, const Config &config, const std::string &output, IdleClock::Time now) {
    IdleVerdict verdict;
    verdict.ambient = ambient_effective_enabled(config, output) && clock.idle(output, ambient_effective_timeout_seconds(config, output), now);
    verdict.screensaver = screensaver_effective_enabled(config, output) && clock.idle(output, screensaver_effective_timeout_seconds(config, output), now);
    return verdict;
}

bool idle_policy_changed(const Config &before, const Config &after) {
    return before.idle_management_enabled != after.idle_management_enabled || before.ambient_enabled != after.ambient_enabled || before.ambient_timeout_seconds != after.ambient_timeout_seconds || before.screensaver_enabled != after.screensaver_enabled || before.screensaver_timeout_seconds != after.screensaver_timeout_seconds || before.monitor_overrides != after.monitor_overrides;
}

} // namespace astralia
