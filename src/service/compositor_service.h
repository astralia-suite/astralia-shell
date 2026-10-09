#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/reactor.h"
#include "core/signal.h"

namespace astralia {

struct Workspace {
    int id = -1;
    std::string name;
    bool occupied = false;

    bool operator==(const Workspace &) const = default;
};

struct MonitorWorkspaces {
    std::vector<Workspace> workspaces;
    int active_id = -1;

    bool operator==(const MonitorWorkspaces &) const = default;
};

struct CompositorMonitor {
    int id = -1;
    std::string name;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    double scale = 1.0;
    int transform = 0;
    std::array<double, 4> reserved{0.0, 0.0, 0.0, 0.0};

    bool operator==(const CompositorMonitor &) const = default;
};

struct CompositorClient {
    std::string address;
    std::string window_class;
    std::string title;
    int workspace_id = -1;
    int monitor_id = -1;
    std::array<double, 2> at{0.0, 0.0};
    std::array<double, 2> size{0.0, 0.0};
    bool floating = false;
    int fullscreen = 0;
    bool pinned = false;
    long focus_history_id = 0;
    bool xwayland = false;

    bool operator==(const CompositorClient &) const = default;
};

struct CompositorState {
    std::unordered_map<std::string, MonitorWorkspaces> by_monitor;
    std::string focused_monitor;
    std::vector<CompositorMonitor> monitors;
    std::vector<CompositorClient> clients;
    int32_t hug_radius_px = 0;

    bool operator==(const CompositorState &) const = default;
};

struct WorkArea {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

WorkArea compositor_work_area(const CompositorMonitor &monitor);
std::array<double, 2> compositor_logical_size(const CompositorMonitor &monitor);
const CompositorMonitor *compositor_monitor(const CompositorState &state, const std::string &name);
const CompositorMonitor *compositor_monitor(const CompositorState &state, int id);

enum class CloseScope { workspace,
                        monitor,
                        all };

class Compositor {
  public:
    virtual ~Compositor() = default;

    virtual const char *name() const = 0;
    virtual const CompositorState &state() const = 0;

    virtual void refresh() = 0;
    virtual bool refresh_clients() = 0;
    virtual void watch_client_order(bool watch) = 0;

    virtual void focus_workspace(int id, bool global = false) = 0;
    virtual void move_window(const std::string &address, int id, bool global = false) = 0;
    virtual void close_window(const std::string &address) = 0;
    virtual void close_windows(CloseScope scope, int id = -1) = 0;
    virtual void move_workspace_in(int id, bool global = false) = 0;
    virtual void swap_workspace(int id, bool global = false) = 0;

    Signal<> active_changed;
    Signal<> structure_changed;
};

class NullCompositor final : public Compositor {
  public:
    const char *name() const override { return "none"; }
    const CompositorState &state() const override { return state_; }
    void refresh() override {}
    bool refresh_clients() override { return false; }
    void watch_client_order(bool) override {}
    void focus_workspace(int, bool = false) override {}
    void move_window(const std::string &, int, bool = false) override {}
    void close_window(const std::string &) override {}
    void close_windows(CloseScope, int = -1) override {}
    void move_workspace_in(int, bool = false) override {}
    void swap_workspace(int, bool = false) override {}

  private:
    CompositorState state_;
};

std::unique_ptr<Compositor> make_compositor(Reactor &loop, std::string_view i3_socket_fallback = {});

} // namespace astralia
