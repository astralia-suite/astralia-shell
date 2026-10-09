#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/json.h"
#include "core/unique_fd.h"

#include "service/compositor_service.h"

namespace astralia {

struct I3Window {
    int64_t id = 0;
    std::string window_class;
    std::string title;
    uint32_t workspace = 0;
    std::string output;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    bool floating = false;
    bool fullscreen = false;
    bool focused = false;

    bool operator==(const I3Window &) const = default;
};

struct I3Workspace {
    uint32_t number = 0;
    std::string name;
    std::string output;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    bool operator==(const I3Workspace &) const = default;
};

struct I3Output {
    std::string name;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    bool operator==(const I3Output &) const = default;
};

struct I3Tree {
    std::vector<I3Output> outputs;
    std::vector<I3Workspace> workspaces;
    std::vector<I3Window> windows;
};

struct I3WorkspaceStatus {
    uint32_t number = 0;
    std::string output;
    bool visible = false;
    bool focused = false;
};

struct I3Message {
    uint32_t type = 0;
    std::string payload;

    bool is_event() const { return (type & 0x80000000u) != 0; }
};

I3Tree i3_parse_tree(const Json &root);
std::vector<I3WorkspaceStatus> i3_parse_workspaces(const Json &array);
CompositorState i3_build_state(const I3Tree &tree, const std::vector<I3WorkspaceStatus> &statuses);
std::string i3_encode(uint32_t type, std::string_view payload);
std::size_t i3_decode(std::string_view buffer, std::vector<I3Message> &out);
std::string i3_socket_path(std::string_view fallback);

class I3Compositor final : public Compositor {
  public:
    I3Compositor(Reactor &loop, std::string socket_path);
    ~I3Compositor() override;
    I3Compositor(const I3Compositor &) = delete;
    I3Compositor &operator=(const I3Compositor &) = delete;

    static bool available(std::string_view fallback);

    const char *name() const override { return "i3"; }
    const CompositorState &state() const override { return state_; }

    void refresh() override;
    bool refresh_clients() override;
    void watch_client_order(bool) override {}

    void focus_workspace(int id, bool global = false) override;
    void move_window(const std::string &address, int id, bool global = false) override;
    void close_window(const std::string &address) override;
    void close_windows(CloseScope scope, int id = -1) override;
    void move_workspace_in(int id, bool global = false) override;
    void swap_workspace(int id, bool global = false) override;

  private:
    std::optional<std::string> request(uint32_t type, std::string_view payload);
    void command(const std::string &text);
    void refresh_active();
    void read_events();
    std::vector<const CompositorClient *> clients_in(int workspace) const;
    void move_all(const std::vector<const CompositorClient *> &windows, const std::string &workspace);

    Reactor &loop_;
    std::string path_;
    CompositorState state_;
    I3Tree tree_;
    UniqueFd event_fd_;
    std::string event_buffer_;
};

} // namespace astralia
