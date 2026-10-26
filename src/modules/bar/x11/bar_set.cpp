#include <algorithm>
#include <map>
#include <vector>

#include "modules/bar/x11/bar.h"
#include "modules/bar/x11/bar_set.h"

namespace astralia::bar {

namespace {

constexpr int32_t status_message_timeout_ms = 6000;

}

BarSet::BarSet(XConnection &x, EventLoop &loop, Shell &shell, Services &services)
    : x_(x), loop_(loop), shell_(shell), services_(services) {
    services_.bluetooth.messages.connect([this](const StatusMessage &message) {
        services_.notifications.post("Bluetooth", message.summary, message.body,
                                     status_message_timeout_ms);
    });
    services_.network.messages.connect([this](const StatusMessage &message) {
        services_.notifications.post("Network", message.summary, message.body,
                                     status_message_timeout_ms);
    });
    shell_.track("bar", [this] {
        return std::ranges::any_of(bars_,
                                   [](const Entry &entry) { return entry.bar->panels_open(); });
    });
    services_.outputs.changed.connect([this] { sync(); });
    services_.settings.changed.connect([this] { sync(); });
    sync();
}

BarSet::~BarSet() = default;

void BarSet::sync() {
    std::vector<std::string> shown;
    for (const Output &output : services_.outputs.outputs()) {
        shown.push_back(output.name);
        auto it = std::ranges::find(bars_, output.name, &Entry::output);
        if (it == bars_.end()) {
            bars_.push_back(
                {output.name, std::make_unique<Bar>(x_, loop_, shell_, services_, output)});
            continue;
        }
        it->bar->place(output);
    }
    for (Entry &entry : bars_) {
        if (std::ranges::find(shown, entry.output) == shown.end()) {
            entry.bar->hide();
        }
    }
}

} // namespace astralia::bar
