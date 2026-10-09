#include <memory>

#include "core/log.h"

#include "service/compositor_service.h"
#include "service/hyprland_service.h"
#include "service/i3_service.h"

namespace astralia {

WorkArea compositor_work_area(const CompositorMonitor &monitor) {
    bool rotated = monitor.transform % 2 == 1;
    double scale = monitor.scale > 0.0 ? monitor.scale : 1.0;
    double logical_width = (rotated ? monitor.height : monitor.width) / scale;
    double logical_height = (rotated ? monitor.width : monitor.height) / scale;
    return {monitor.x + monitor.reserved[0], monitor.y + monitor.reserved[1],
            logical_width - (rotated ? monitor.reserved[1] : monitor.reserved[0]) - (rotated ? monitor.reserved[3] : monitor.reserved[2]),
            logical_height - (rotated ? monitor.reserved[0] : monitor.reserved[1]) - (rotated ? monitor.reserved[2] : monitor.reserved[3])};
}

std::array<double, 2> compositor_logical_size(const CompositorMonitor &monitor) {
    bool rotated = monitor.transform % 2 == 1;
    double scale = monitor.scale > 0.0 ? monitor.scale : 1.0;
    return {(rotated ? monitor.height : monitor.width) / scale, (rotated ? monitor.width : monitor.height) / scale};
}

const CompositorMonitor *compositor_monitor(const CompositorState &state, const std::string &name) {
    for (const CompositorMonitor &monitor : state.monitors) {
        if (monitor.name == name) {
            return &monitor;
        }
    }
    return nullptr;
}

const CompositorMonitor *compositor_monitor(const CompositorState &state, int id) {
    for (const CompositorMonitor &monitor : state.monitors) {
        if (monitor.id == id) {
            return &monitor;
        }
    }
    return nullptr;
}

std::unique_ptr<Compositor> make_compositor(Reactor &loop, std::string_view i3_socket_fallback) {
    std::unique_ptr<Compositor> compositor;
    if (HyprlandCompositor::available()) {
        compositor = std::make_unique<HyprlandCompositor>(loop);
    } else if (I3Compositor::available(i3_socket_fallback)) {
        compositor = std::make_unique<I3Compositor>(loop, i3_socket_path(i3_socket_fallback));
    }
    if (!compositor) {
        log::info("compositor backend: none");
        return std::make_unique<NullCompositor>();
    }
    log::info("compositor backend: {}", compositor->name());
    return compositor;
}

} // namespace astralia
