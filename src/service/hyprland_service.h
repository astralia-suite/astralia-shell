#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/unique_fd.h"

#include "service/compositor_service.h"

namespace astralia {

enum class HyprEventKind { none,
                           active,
                           structural };

std::vector<CompositorClient> hypr_parse_clients(std::string_view reply);
bool hypr_client_order_differs(const std::vector<CompositorClient> &a, const std::vector<CompositorClient> &b);
void hypr_parse_workspaces(std::string_view reply, CompositorState &state);
void hypr_parse_monitors(std::string_view reply, CompositorState &state);
int32_t hypr_parse_gaps_out(std::string_view reply);
int32_t hypr_parse_rounding(std::string_view reply);
HyprEventKind hypr_apply_event(CompositorState &state, std::string_view line);
int hypr_resolve_workspace(const CompositorState &state, int id, bool global);
std::string hypr_lua_focus(int workspace);
std::string hypr_lua_move(const std::string &address, int workspace, bool follow);
std::string hypr_lua_close(const std::string &address);
bool hypr_socket_paths(std::string &request_path, std::string &event_path);

class HyprlandCompositor final : public Compositor {
  public:
    explicit HyprlandCompositor(Reactor &loop);
    ~HyprlandCompositor() override;
    HyprlandCompositor(const HyprlandCompositor &) = delete;
    HyprlandCompositor &operator=(const HyprlandCompositor &) = delete;

    static bool available();

    const char *name() const override { return "Hyprland"; }
    const CompositorState &state() const override { return state_; }

    void refresh() override;
    bool refresh_clients() override;
    void watch_client_order(bool watch) override;

    void focus_workspace(int id, bool global = false) override;
    void move_window(const std::string &address, int id, bool global = false) override;
    void close_window(const std::string &address) override;
    void close_windows(CloseScope scope, int id = -1) override;
    void move_workspace_in(int id, bool global = false) override;
    void swap_workspace(int id, bool global = false) override;

    void set_request_socket(std::string path) { request_path_ = std::move(path); }

  private:
    void read_events();
    void dispatch(const std::string &lua);
    std::vector<const CompositorClient *> clients_in(int workspace) const;
    void move_all(const std::vector<const CompositorClient *> &windows, const std::string &workspace_lua);

    Reactor &loop_;
    CompositorState state_;
    std::string request_path_;
    std::string event_path_;
    std::string clients_reply_;
    std::string event_buffer_;
    UniqueFd event_fd_;
    bool watching_ = false;
    int timer_ = -1;
};

} // namespace astralia
