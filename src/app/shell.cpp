#include "app/shell.h"

namespace astralia {

namespace {

constexpr std::array<ShellVerbSpec, static_cast<size_t>(ShellVerb::count)> verbs{{
    {"launcher", "toggle the launcher, searching from $HOME", nullptr},
    {"launcher global", "toggle the launcher, searching from /", nullptr},
    {"logout", "toggle the logout overlay", nullptr},
    {"overview", "toggle the overview", nullptr},
    {"settings", "toggle the settings overlay", nullptr},
    {"dashboard", "toggle the dashboard window", &Capabilities::dashboard},
    {"rain", "toggle the rain overlay", &Capabilities::rain},
    {"visualizer", "toggle the audio visualizer overlay", &Capabilities::visualizer},
    {"lock", "lock the session", &Capabilities::lock},
    {"panel-tray", "toggle the tray panel", nullptr},
    {"panel-resource", "toggle the resource panel", &Capabilities::resource_panel},
    {"panel-network", "toggle the network panel", nullptr},
    {"panel-bluetooth", "toggle the bluetooth panel", nullptr},
    {"panel-volume", "toggle the volume panel", nullptr},
    {"panel-battery", "toggle the battery panel", nullptr},
    {"panel-media", "toggle the media panel", nullptr},
    {"panel-brightness", "toggle the brightness panel", nullptr},
    {"panel-clock", "toggle the clock panel", nullptr},
}};

} // namespace

std::string shell_report() {
    Capabilities wayland = wayland_capabilities();
    Capabilities x11 = x11_capabilities();
    auto mark = [](bool value) { return value ? "yes" : "no"; };
    std::string text = "| Module or feature | Wayland | X11 |\n| --- | --- | --- |\n";
    for (const char *name : {"bar", "launcher", "logout", "overview", "settings", "notification", "osd", "polkit", "wallpaper"}) {
        text += std::string("| `") + name + "` | yes | yes |\n";
    }
    constexpr std::pair<const char *, bool Capabilities::*> features[] = {
        {"lock", &Capabilities::lock},
        {"idle", &Capabilities::idle},
        {"dashboard", &Capabilities::dashboard},
        {"rain", &Capabilities::rain},
        {"visualizer", &Capabilities::visualizer},
        {"animated_wallpaper", &Capabilities::animated_wallpaper},
        {"resource_panel", &Capabilities::resource_panel},
        {"animations", &Capabilities::animations},
    };
    for (const auto &[name, member] : features) {
        text += std::string("| `") + name + "` | " + mark(wayland.*member) + " | " + mark(x11.*member) + " |\n";
    }
    text += "\n| Verb | Description | Wayland | X11 |\n| --- | --- | --- | --- |\n";
    for (int i = 0; i < static_cast<int>(ShellVerb::count); ++i) {
        ShellVerb verb = static_cast<ShellVerb>(i);
        const ShellVerbSpec &spec = shell_verb_spec(verb);
        text += std::string("| `") + spec.name + "` | " + spec.description + " | " + mark(shell_verb_available(wayland, verb)) + " | " + mark(shell_verb_available(x11, verb)) + " |\n";
    }
    return text;
}

Capabilities wayland_capabilities() {
    return {true, true, true, true, true, true, true, true};
}

Capabilities x11_capabilities() {
    return {};
}

const ShellVerbSpec &shell_verb_spec(ShellVerb verb) {
    return verbs[static_cast<size_t>(verb)];
}

bool shell_verb_available(const Capabilities &capabilities, ShellVerb verb) {
    const ShellVerbSpec &spec = shell_verb_spec(verb);
    return spec.requires_capability == nullptr || capabilities.*spec.requires_capability;
}

bool Shell::bind(ShellVerb verb, std::function<void()> run) {
    if (!shell_verb_available(capabilities_, verb)) {
        return false;
    }
    bindings_[static_cast<size_t>(verb)] = std::move(run);
    return true;
}

void Shell::bind(std::vector<ShellBinding> bindings) {
    for (ShellBinding &binding : bindings) {
        bind(binding.verb, std::move(binding.run));
    }
}

bool Shell::bound(ShellVerb verb) const {
    return static_cast<bool>(bindings_[static_cast<size_t>(verb)]);
}

bool Shell::run(ShellVerb verb) {
    const auto &fn = bindings_[static_cast<size_t>(verb)];
    if (!fn) {
        return false;
    }
    fn();
    if (after_) {
        after_();
    }
    return true;
}

void Shell::track(std::string name, std::function<bool()> is_open) {
    tracked_.push_back({std::move(name), std::move(is_open)});
}

bool Shell::any_open() const {
    for (const Tracked &tracked : tracked_) {
        if (tracked.is_open()) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> Shell::open_modules() const {
    std::vector<std::string> names;
    for (const Tracked &tracked : tracked_) {
        if (tracked.is_open()) {
            names.push_back(tracked.name);
        }
    }
    return names;
}

std::vector<IpcHandler> Shell::handlers() {
    std::vector<IpcHandler> out;
    out.push_back({"status", [this] { return status(); }, "show the backend, its capabilities and the open modules"});
    for (size_t i = 0; i < bindings_.size(); ++i) {
        if (!bindings_[i]) {
            continue;
        }
        ShellVerb verb = static_cast<ShellVerb>(i);
        out.push_back({verbs[i].name, [this, verb] {
                           run(verb);
                           return std::string();
                       },
                       verbs[i].description});
    }
    return out;
}

std::string Shell::status() const {
    constexpr std::pair<const char *, bool Capabilities::*> names[] = {
        {"lock", &Capabilities::lock},
        {"idle", &Capabilities::idle},
        {"dashboard", &Capabilities::dashboard},
        {"rain", &Capabilities::rain},
        {"visualizer", &Capabilities::visualizer},
        {"animated_wallpaper", &Capabilities::animated_wallpaper},
        {"resource_panel", &Capabilities::resource_panel},
        {"animations", &Capabilities::animations},
    };
    std::string text = "backend: " + backend_ + "\ncapabilities:";
    bool any = false;
    for (const auto &[name, member] : names) {
        if (capabilities_.*member) {
            text += std::string(" ") + name;
            any = true;
        }
    }
    text += any ? "\n" : " none\n";
    text += "open:";
    std::vector<std::string> open = open_modules();
    for (const std::string &name : open) {
        text += " " + name;
    }
    text += open.empty() ? " none\n" : "\n";
    return text;
}

void Shell::attach(IpcServer &server) {
    for (IpcHandler &handler : handlers()) {
        server.add(std::move(handler));
    }
}

} // namespace astralia
