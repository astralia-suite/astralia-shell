#include <utility>
#include <vector>

#include "wayland/app/ipc.h"
#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_state.h"

std::vector<astralia::ShellBinding> collect_shell_bindings(WaylandState &state) {
    std::vector<astralia::ShellBinding> handlers;
    auto append = [&handlers](std::vector<astralia::ShellBinding> module_handlers) {
        for (astralia::ShellBinding &h : module_handlers)
            handlers.push_back(std::move(h));
    };
    for (auto &m : state.overlays)
        append(m->shell_bindings(state));
    if (!state.outputs.empty())
        for (auto &m : state.outputs.front()->modules)
            append(m->shell_bindings(state));
    return handlers;
}
