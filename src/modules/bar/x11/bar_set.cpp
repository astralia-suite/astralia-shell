#include <algorithm>
#include <map>
#include <vector>

#include "core/log.h"

#include "modules/bar/panel/panel_set.h"
#include "modules/bar/x11/bar.h"
#include "modules/bar/x11/bar_set.h"

namespace astralia {

BarSet::BarSet(XConnection &x, EventLoop &loop, Shell &shell, Services &services)
    : x_(x), loop_(loop), shell_(shell), services_(services) {
    notifier_ =
        services_.session.proxy("org.freedesktop.Notifications", "/org/freedesktop/Notifications");
    services_.bluetooth.messages.connect(
        [this](const StatusMessage &message) { notify("Bluetooth", message); });
    services_.network.messages.connect(
        [this](const StatusMessage &message) { notify("Network", message); });
    constexpr std::pair<ShellVerb, PanelId> verbs[] = {
        {ShellVerb::panel_tray, PanelId::tray},
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
            const std::string &target = services_.outputs.at_pointer().name;
            auto it = std::ranges::find(bars_, target, &Entry::output);
            if (it != bars_.end()) {
                it->bar->toggle_panel(panel);
            }
        });
    }
    shell_.track("bar", [this] {
        return std::ranges::any_of(bars_, [](const Entry &entry) { return entry.bar->panels_open(); });
    });
    services_.outputs.changed.connect([this] { sync(); });
    services_.settings.changed.connect([this] { sync(); });
    sync();
}

BarSet::~BarSet() = default;

void BarSet::sync() {
    std::vector<std::string> shown;
    for (const Output &output : services_.outputs.outputs()) {
        if (!bar_effective_enabled(services_.settings.config(), output.name)) {
            continue;
        }
        shown.push_back(output.name);
        auto it = std::ranges::find(bars_, output.name, &Entry::output);
        if (it == bars_.end()) {
            bars_.push_back({output.name, std::make_unique<Bar>(x_, loop_, shell_, services_, output)});
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

void BarSet::notify(const std::string &app, const StatusMessage &message) {
    if (!notifier_) {
        return;
    }
    try {
        notifier_->callMethod("Notify")
            .onInterface("org.freedesktop.Notifications")
            .withArguments(app, uint32_t{0}, std::string(), message.summary, message.body,
                           std::vector<std::string>{}, std::map<std::string, sdbus::Variant>{},
                           int32_t{-1})
            .dontExpectReply();
    } catch (const sdbus::Error &error) {
        log::error("bar: cannot send notification: {}", error.what());
    }
}

} // namespace astralia
