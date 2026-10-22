#include <algorithm>
#include <map>
#include <vector>

#include "app/active_target.h"

#include "modules/bar/panel/panel_set.h"
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
    constexpr std::pair<ShellVerb, PanelId> verbs[] = {
        {ShellVerb::panel_tray, PanelId::tray},
        {ShellVerb::panel_resource, PanelId::resource},
        {ShellVerb::panel_network, PanelId::network},
        {ShellVerb::panel_bluetooth, PanelId::bluetooth},
        {ShellVerb::panel_volume, PanelId::volume},
        {ShellVerb::panel_battery, PanelId::battery},
        {ShellVerb::panel_media, PanelId::media},
        {ShellVerb::panel_brightness, PanelId::brightness},
        {ShellVerb::panel_clock, PanelId::clock},
    };
    for (const auto &[verb, panel] : verbs) {
        shell_.bind(verb, [this, panel] {
            const std::string &target = active_target_output(services_).name;
            auto it = std::ranges::find(bars_, target, &Entry::output);
            if (it != bars_.end()) {
                it->bar->toggle_panel(panel);
            }
        });
    }
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
