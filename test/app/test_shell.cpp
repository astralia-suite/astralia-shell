#include <set>
#include <string>

#include "app/shell.h"

#include "check.h"

void check_shell() {
    using namespace astralia;
    using test::check;

    std::set<std::string> names;
    for (int i = 0; i < static_cast<int>(ShellVerb::count); ++i) {
        names.insert(shell_verb_spec(static_cast<ShellVerb>(i)).name);
    }
    check(names.size() == static_cast<size_t>(ShellVerb::count), "every verb has its own name");
    check(std::string(shell_verb_spec(ShellVerb::launcher_global).name) == "launcher global", "the launcher keeps its two-word verb");

    Capabilities wayland = wayland_capabilities();
    Capabilities x11 = x11_capabilities();
    check(shell_verb_available(wayland, ShellVerb::dashboard) && !shell_verb_available(x11, ShellVerb::dashboard), "the dashboard needs its capability");
    check(shell_verb_available(wayland, ShellVerb::panel_resource) && !shell_verb_available(x11, ShellVerb::panel_resource), "the resource panel needs its capability");
    check(shell_verb_available(x11, ShellVerb::overview) && shell_verb_available(x11, ShellVerb::panel_network), "shared verbs need none");

    Shell shell("x11", x11);
    int calls = 0;
    int after = 0;
    shell.after_verb([&] { ++after; });
    check(shell.bind(ShellVerb::overview, [&] { ++calls; }), "a shared verb binds");
    check(!shell.bind(ShellVerb::rain, [&] { ++calls; }) && !shell.bound(ShellVerb::rain), "a verb the backend lacks is refused");
    check(shell.run(ShellVerb::overview) && calls == 1 && after == 1, "running calls the module and the hook");
    check(!shell.run(ShellVerb::logout) && after == 1, "an unbound verb does nothing");

    shell.bind({{ShellVerb::logout, [&] { calls += 10; }}, {ShellVerb::dashboard, [&] { calls += 100; }}});
    check(shell.bound(ShellVerb::logout) && !shell.bound(ShellVerb::dashboard), "a binding list is filtered by capability");

    std::vector<IpcHandler> handlers = shell.handlers();
    check(handlers.size() == 3 && handlers[0].verb == "status" && handlers[1].verb == "logout" && handlers[2].verb == "overview" && !handlers[1].description.empty(), "handlers come in verb order with descriptions");
    handlers[1].fn();
    check(calls == 11, "a handler runs its binding");

    bool open = false;
    shell.track("overview", [&] { return open; });
    shell.track("launcher", [] { return true; });
    check(shell.any_open() && shell.open_modules() == std::vector<std::string>{"launcher"}, "open modules are listed");
    open = true;
    check(shell.open_modules().size() == 2, "a module that opens is listed too");
    check(shell.status() == "backend: x11\ncapabilities: none\nopen: overview launcher\n", "the status lists the backend, the capabilities and the open modules");
    Shell wayland_shell("wayland", wayland);
    check(wayland_shell.status().find("capabilities: lock idle dashboard rain visualizer animated_wallpaper resource_panel animations") != std::string::npos && wayland_shell.status().ends_with("open: none\n"), "a Wayland shell reports every capability");
    check(wayland_shell.handlers().front().verb == "status", "the status verb is always offered");

    std::string report = shell_report();
    check(report.find("| `dashboard` | yes | no |") != std::string::npos && report.find("| `launcher` | yes | yes |") != std::string::npos, "the report lists modules by capability");
    check(report.find("| `panel-resource` | toggle the resource panel | yes | no |") != std::string::npos && report.find("| `overview` | toggle the overview | yes | yes |") != std::string::npos, "the report lists verbs by capability");
}
