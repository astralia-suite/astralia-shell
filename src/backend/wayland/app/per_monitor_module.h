#pragma once

#include "wayland/app/config.h"

#include <vector>
#include <wayland-client.h>

#include "wayland/app/ipc.h"

struct WaylandState;
struct MonitorOutput;
struct KeyEvent;

class PerMonitorModule {
  public:
    virtual ~PerMonitorModule() = default;

    virtual bool create_surface(WaylandState &app, MonitorOutput &mon, wl_output *output) = 0;
    virtual bool configured() const = 0;
    virtual bool init_egl(WaylandState &app, MonitorOutput &mon) = 0;
    virtual void destroy(WaylandState &app, MonitorOutput &mon) = 0;
    virtual bool owns_surface(wl_surface *surface) const = 0;

    virtual void request_frame() {}
    virtual void apply_config(WaylandState &, MonitorOutput &, const Config &) {}
    virtual void tick(WaylandState &, MonitorOutput &) {}
    virtual void timer_tick(WaylandState &, MonitorOutput &) {}
    virtual bool is_open() const { return false; }
    virtual std::vector<astralia::ShellBinding> shell_bindings(WaylandState &) { return {}; }
    virtual void handle_click(WaylandState &, MonitorOutput &, wl_surface *, int, double, double, uint32_t) {}
    virtual void handle_scroll(WaylandState &, MonitorOutput &, wl_surface *, double) {}
    virtual void handle_key_event(WaylandState &, MonitorOutput &, const KeyEvent &) {}
    virtual void handle_pointer_move(WaylandState &, MonitorOutput &, double, double) {}
    virtual void handle_pointer_release() {}
    virtual bool wants_pointing_hand_cursor() const { return false; }
};
