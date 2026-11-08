#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "core/json.h"
#include "core/unique_fd.h"

#include "service/compositor_service.h"

namespace astralia {

// Fallback for a shojiwm that doesn't send workspacesPerMonitor.
inline constexpr int shoji_default_workspaces_per_monitor = 5;

struct ShojiWorkspaceRef {
    std::string monitor;
    int index = 0;

    bool operator==(const ShojiWorkspaceRef &) const = default;
};

std::string shoji_socket_path(std::string_view runtime_dir, std::string_view wayland_display);
CompositorState shoji_parse_view(const Json &view);
int shoji_per_monitor(const CompositorState &state);
int shoji_workspace_id(int per_monitor, int monitor_ordinal, int index);
int shoji_resolve_workspace(const CompositorState &state, int id, bool global);
std::optional<ShojiWorkspaceRef> shoji_workspace_ref(const CompositorState &state, int id);
std::string shoji_request(int id, std::string_view method, const Json &params);
bool shoji_structure_differs(const CompositorState &a, const CompositorState &b);
Json shoji_border_params(Color active, Color inactive);

class ShojiCompositor final : public Compositor {
  public:
    ShojiCompositor(Reactor &loop, std::string socket_path);
    ~ShojiCompositor() override;
    ShojiCompositor(const ShojiCompositor &) = delete;
    ShojiCompositor &operator=(const ShojiCompositor &) = delete;

    static std::string default_socket_path();
    static bool available();

    const char *name() const override { return "ShojiWM"; }
    const CompositorState &state() const override { return state_; }

    void refresh() override;
    bool refresh_clients() override;
    void watch_client_order(bool) override {}
    int workspaces_per_monitor() const override { return shoji_per_monitor(state_); }
    bool capture_by_toplevel_handle() const override { return true; }

    void focus_workspace(int id, bool global = false) override;
    void move_window(const std::string &address, int id, bool global = false) override;
    void close_window(const std::string &address) override;
    void close_windows(CloseScope scope, int id = -1) override;
    void move_workspace_in(int id, bool global = false) override;
    void swap_workspace(int id, bool global = false) override;
    void apply_border_colors(Color active, Color inactive) override;

  private:
    std::optional<Json> call(std::string_view method, const Json &params);
    void workspace_call(std::string_view method, int id, bool global);
    void connect_events();
    void read_events();
    void apply(CompositorState next);

    Reactor &loop_;
    std::string path_;
    CompositorState state_;
    UniqueFd event_fd_;
    std::string event_buffer_;
    std::optional<Json> border_params_;
    int next_id_ = 1;
};

} // namespace astralia
