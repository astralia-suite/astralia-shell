#pragma once

#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "core/ipc.h"

namespace astralia {

struct Capabilities {
    bool lock = false;
    bool idle = false;
    bool dashboard = false;
    bool rain = false;
    bool visualizer = false;
    bool animated_wallpaper = false;
    bool resource_panel = false;
    bool animations = false;
};

Capabilities wayland_capabilities();
Capabilities x11_capabilities();

enum class ShellVerb : int {
    launcher,
    launcher_global,
    logout,
    overview,
    settings,
    dashboard,
    rain,
    visualizer,
    lock,
    panel_tray,
    panel_resource,
    panel_network,
    panel_bluetooth,
    panel_volume,
    panel_battery,
    panel_media,
    panel_brightness,
    panel_clock,
    count
};

struct ShellVerbSpec {
    const char *name;
    const char *description;
    bool Capabilities::*requires_capability;
};

const ShellVerbSpec &shell_verb_spec(ShellVerb verb);
std::string shell_report();
bool shell_verb_available(const Capabilities &capabilities, ShellVerb verb);

struct ShellBinding {
    ShellVerb verb;
    std::function<void()> run;
};

class Shell {
  public:
    Shell(std::string backend, Capabilities capabilities) : backend_(std::move(backend)), capabilities_(capabilities) {}

    const Capabilities &capabilities() const { return capabilities_; }

    bool bind(ShellVerb verb, std::function<void()> run);
    void bind(std::vector<ShellBinding> bindings);
    bool run(ShellVerb verb);
    bool bound(ShellVerb verb) const;
    void after_verb(std::function<void()> hook) { after_ = std::move(hook); }

    void track(std::string name, std::function<bool()> is_open);
    bool any_open() const;
    std::vector<std::string> open_modules() const;

    std::string status() const;
    void attach(IpcServer &server);
    std::vector<IpcHandler> handlers();

  private:
    struct Tracked {
        std::string name;
        std::function<bool()> is_open;
    };

    std::string backend_;
    Capabilities capabilities_;
    std::array<std::function<void()>, static_cast<size_t>(ShellVerb::count)> bindings_{};
    std::vector<Tracked> tracked_;
    std::function<void()> after_;
};

} // namespace astralia
