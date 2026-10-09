#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "core/json.h"
#include "core/log.h"

#include "service/hyprland_service.h"

namespace astralia {

namespace {

constexpr timeval request_timeout{0, 100000};
constexpr int workspaces_per_monitor = 10;
constexpr const char *swap_temp_workspace = "special:__tmp_swp";
constexpr std::chrono::milliseconds client_poll_interval{1000};
constexpr std::chrono::milliseconds client_poll_idle = std::chrono::hours(1);

int connect_socket(const std::string &path) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

std::string request(const std::string &socket_path, const std::string &command) {
    if (socket_path.empty()) {
        return {};
    }
    int fd = connect_socket(socket_path);
    if (fd < 0) {
        return {};
    }
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &request_timeout, sizeof request_timeout);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &request_timeout, sizeof request_timeout);
    std::size_t sent = 0;
    while (sent < command.size()) {
        ssize_t n = send(fd, command.data() + sent, command.size() - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            return {};
        }
        sent += static_cast<std::size_t>(n);
    }
    shutdown(fd, SHUT_WR);
    std::string result;
    char buffer[4096];
    for (;;) {
        ssize_t n = recv(fd, buffer, sizeof buffer, 0);
        if (n > 0) {
            result.append(buffer, static_cast<std::size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            result.clear();
        }
        break;
    }
    close(fd);
    return result;
}

std::vector<std::string_view> split(std::string_view text, char delimiter) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        std::size_t position = text.find(delimiter, start);
        if (position == std::string_view::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, position - start));
        start = position + 1;
    }
    return parts;
}

int to_int(std::string_view text, int fallback) {
    int value = fallback;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

double pair_value(const Json *array, std::size_t index, double fallback) {
    if (array == nullptr || array->type != Json::Type::array || index >= array->array.size() ||
        array->array[index].type != Json::Type::number) {
        return fallback;
    }
    return array->array[index].number;
}

} // namespace

bool hypr_socket_paths(std::string &request_path, std::string &event_path) {
    const char *signature = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (signature == nullptr || *signature == '\0') {
        return false;
    }
    std::string directory;
    const char *runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (runtime_dir != nullptr && *runtime_dir != '\0') {
        directory = std::string(runtime_dir) + "/hypr/" + signature;
    }
    std::error_code error;
    if (directory.empty() || !std::filesystem::is_directory(directory, error)) {
        directory = std::string("/tmp/hypr/") + signature;
    }
    if (!std::filesystem::is_directory(directory, error)) {
        return false;
    }
    request_path = directory + "/.socket.sock";
    event_path = directory + "/.socket2.sock";
    return true;
}

std::vector<CompositorClient> hypr_parse_clients(std::string_view reply) {
    std::vector<CompositorClient> clients;
    std::optional<Json> parsed = parse_json(reply);
    if (!parsed || parsed->type != Json::Type::array) {
        return clients;
    }
    for (const Json &entry : parsed->array) {
        CompositorClient client;
        client.address = entry.string_or("address");
        client.window_class = entry.string_or("class");
        client.title = entry.string_or("title");
        if (const Json *workspace = entry.find("workspace")) {
            client.workspace_id = static_cast<int>(workspace->number_or("id", -1));
        }
        client.monitor_id = static_cast<int>(entry.number_or("monitor", -1));
        const Json *at = entry.find("at");
        client.at = {pair_value(at, 0, 0.0), pair_value(at, 1, 0.0)};
        const Json *size = entry.find("size");
        client.size = {pair_value(size, 0, 100.0), pair_value(size, 1, 100.0)};
        client.floating = entry.boolean_or("floating", false);
        client.fullscreen = static_cast<int>(entry.number_or("fullscreen", 0));
        client.pinned = entry.boolean_or("pinned", false);
        client.focus_history_id = static_cast<long>(entry.number_or("focusHistoryID", 0));
        client.xwayland = entry.boolean_or("xwayland", false);
        clients.push_back(std::move(client));
    }
    return clients;
}

bool hypr_client_order_differs(const std::vector<CompositorClient> &a, const std::vector<CompositorClient> &b) {
    if (a.size() != b.size()) {
        return true;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].address != b[i].address || a[i].workspace_id != b[i].workspace_id ||
            a[i].at[0] != b[i].at[0] || a[i].focus_history_id != b[i].focus_history_id) {
            return true;
        }
    }
    return false;
}

void hypr_parse_workspaces(std::string_view reply, CompositorState &state) {
    std::optional<Json> parsed = parse_json(reply);
    if (!parsed || parsed->type != Json::Type::array) {
        return;
    }
    state.by_monitor.clear();
    for (const Json &entry : parsed->array) {
        Workspace workspace;
        workspace.id = static_cast<int>(entry.number_or("id", -1));
        workspace.name = entry.string_or("name");
        if (workspace.id < 0) {
            continue;
        }
        workspace.occupied = entry.number_or("windows", 0) > 0;
        state.by_monitor[entry.string_or("monitor")].workspaces.push_back(std::move(workspace));
    }
    for (auto &[monitor, entry] : state.by_monitor) {
        std::ranges::sort(entry.workspaces, {}, &Workspace::id);
    }
}

void hypr_parse_monitors(std::string_view reply, CompositorState &state) {
    std::optional<Json> parsed = parse_json(reply);
    if (!parsed || parsed->type != Json::Type::array) {
        return;
    }
    state.monitors.clear();
    for (const Json &entry : parsed->array) {
        std::string name = entry.string_or("name");
        if (const Json *workspace = entry.find("activeWorkspace")) {
            state.by_monitor[name].active_id = static_cast<int>(workspace->number_or("id", -1));
        }
        if (entry.boolean_or("focused", false)) {
            state.focused_monitor = name;
        }
        CompositorMonitor monitor;
        monitor.id = static_cast<int>(entry.number_or("id", -1));
        monitor.name = name;
        monitor.x = entry.number_or("x", 0.0);
        monitor.y = entry.number_or("y", 0.0);
        monitor.width = entry.number_or("width", 0.0);
        monitor.height = entry.number_or("height", 0.0);
        monitor.scale = entry.number_or("scale", 1.0);
        monitor.transform = static_cast<int>(entry.number_or("transform", 0));
        const Json *reserved = entry.find("reserved");
        for (std::size_t i = 0; i < monitor.reserved.size(); ++i) {
            monitor.reserved[i] = pair_value(reserved, i, 0.0);
        }
        state.monitors.push_back(std::move(monitor));
    }
}

int32_t hypr_parse_gaps_out(std::string_view reply) {
    std::optional<Json> parsed = parse_json(reply);
    if (!parsed) {
        return 0;
    }
    std::string css = parsed->string_or("css");
    return css.empty() ? 0 : to_int(css, 0);
}

int32_t hypr_parse_rounding(std::string_view reply) {
    std::optional<Json> parsed = parse_json(reply);
    return parsed ? static_cast<int32_t>(parsed->number_or("int", 0)) : 0;
}

HyprEventKind hypr_apply_event(CompositorState &state, std::string_view line) {
    std::size_t separator = line.find(">>");
    if (separator == std::string_view::npos) {
        return HyprEventKind::none;
    }
    std::string_view event = line.substr(0, separator);
    std::string_view data = line.substr(separator + 2);
    if (event == "focusedmonv2") {
        std::vector<std::string_view> parts = split(data, ',');
        if (parts.size() >= 2) {
            state.focused_monitor = std::string(parts[0]);
            state.by_monitor[state.focused_monitor].active_id = to_int(parts[1], -1);
            return HyprEventKind::active;
        }
        return HyprEventKind::none;
    }
    if (event == "workspacev2") {
        std::vector<std::string_view> parts = split(data, ',');
        if (!parts.empty() && !state.focused_monitor.empty()) {
            state.by_monitor[state.focused_monitor].active_id = to_int(parts[0], -1);
            return HyprEventKind::active;
        }
        return HyprEventKind::none;
    }
    static constexpr std::string_view structural[] = {
        "createworkspacev2", "destroyworkspacev2", "renameworkspace", "moveworkspacev2", "openwindow",
        "closewindow", "movewindow", "movewindowv2", "pin", "fullscreen", "changefloatingmode",
        "activewindowv2", "moveintogroup", "moveoutofgroup", "togglegroup", "changegroupactivev2"};
    return std::ranges::find(structural, event) != std::end(structural) ? HyprEventKind::structural
                                                                        : HyprEventKind::none;
}

int hypr_resolve_workspace(const CompositorState &state, int id, bool global) {
    if (global || id < 1 || id > workspaces_per_monitor) {
        return id;
    }
    int active = 1;
    auto it = state.by_monitor.find(state.focused_monitor);
    if (it != state.by_monitor.end() && it->second.active_id > 0) {
        active = it->second.active_id;
    }
    int base = ((active - 1) / workspaces_per_monitor) * workspaces_per_monitor;
    return base + id;
}

std::string hypr_lua_focus(int workspace) {
    return "hl.dsp.focus({workspace=" + std::to_string(workspace) + "})";
}

std::string hypr_lua_move(const std::string &address, int workspace, bool follow) {
    std::string target = address.empty() ? "activewindow" : "address:" + address;
    return "hl.dsp.window.move({window='" + target + "', workspace=" + std::to_string(workspace) +
           ", follow=" + (follow ? "true" : "false") + "})";
}

std::string hypr_lua_close(const std::string &address) {
    return "hl.dsp.window.close({window='address:" + address + "'})";
}

bool HyprlandCompositor::available() {
    std::string request_path;
    std::string event_path;
    return hypr_socket_paths(request_path, event_path);
}

HyprlandCompositor::HyprlandCompositor(Reactor &loop) : loop_(loop) {
    if (!hypr_socket_paths(request_path_, event_path_)) {
        log::info("hyprland: HYPRLAND_INSTANCE_SIGNATURE not set");
        return;
    }
    refresh();
    state_.hug_radius_px = hypr_parse_gaps_out(request(request_path_, "j/getoption general:gaps_out")) +
                           hypr_parse_rounding(request(request_path_, "j/getoption decoration:rounding"));
    int fd = connect_socket(event_path_);
    if (fd < 0) {
        log::error("hyprland: cannot connect the event socket: {}", std::strerror(errno));
    } else {
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        event_fd_ = UniqueFd(fd);
        loop_.on_fd(event_fd_.get(), [this] { read_events(); });
    }
    timer_ = loop_.add_timer(
        [this] { return watching_ ? client_poll_interval : client_poll_idle; },
        [this] {
            if (watching_ && refresh_clients()) {
                structure_changed.emit();
            }
        });
}

HyprlandCompositor::~HyprlandCompositor() {
    if (event_fd_.get() >= 0) {
        loop_.remove_fd(event_fd_.get());
    }
}

void HyprlandCompositor::refresh() {
    if (request_path_.empty()) {
        return;
    }
    std::string workspaces = request(request_path_, "j/workspaces");
    std::string monitors = request(request_path_, "j/monitors");
    std::string clients = request(request_path_, "j/clients");
    if (workspaces.empty() || monitors.empty() || clients.empty()) {
        return;
    }
    hypr_parse_workspaces(workspaces, state_);
    hypr_parse_monitors(monitors, state_);
    state_.clients = hypr_parse_clients(clients);
    clients_reply_ = std::move(clients);
}

bool HyprlandCompositor::refresh_clients() {
    if (request_path_.empty()) {
        return false;
    }
    std::string reply = request(request_path_, "j/clients");
    if (reply.empty() || reply == clients_reply_) {
        return false;
    }
    std::vector<CompositorClient> fresh = hypr_parse_clients(reply);
    clients_reply_ = std::move(reply);
    if (!hypr_client_order_differs(fresh, state_.clients)) {
        return false;
    }
    state_.clients = std::move(fresh);
    return true;
}

void HyprlandCompositor::watch_client_order(bool watch) {
    if (watching_ == watch) {
        return;
    }
    watching_ = watch;
    if (timer_ >= 0) {
        loop_.reschedule(timer_);
    }
}

void HyprlandCompositor::read_events() {
    char buffer[4096];
    ssize_t n = 0;
    while ((n = recv(event_fd_.get(), buffer, sizeof buffer, MSG_DONTWAIT)) > 0) {
        event_buffer_.append(buffer, static_cast<std::size_t>(n));
    }
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        log::error("hyprland: the event socket closed");
        loop_.remove_fd(event_fd_.get());
        event_fd_ = UniqueFd();
        return;
    }
    HyprEventKind result = HyprEventKind::none;
    std::size_t newline = 0;
    while ((newline = event_buffer_.find('\n')) != std::string::npos) {
        HyprEventKind kind = hypr_apply_event(state_, std::string_view(event_buffer_).substr(0, newline));
        event_buffer_.erase(0, newline + 1);
        if (kind == HyprEventKind::structural || (kind == HyprEventKind::active && result == HyprEventKind::none)) {
            result = kind;
        }
    }
    if (result == HyprEventKind::structural) {
        refresh();
        structure_changed.emit();
    } else if (result == HyprEventKind::active) {
        active_changed.emit();
    }
}

void HyprlandCompositor::dispatch(const std::string &lua) {
    request(request_path_, "dispatch " + lua);
}

std::vector<const CompositorClient *> HyprlandCompositor::clients_in(int workspace) const {
    std::vector<const CompositorClient *> out;
    for (const CompositorClient &client : state_.clients) {
        if (client.workspace_id == workspace) {
            out.push_back(&client);
        }
    }
    return out;
}

void HyprlandCompositor::move_all(const std::vector<const CompositorClient *> &windows, const std::string &workspace_lua) {
    for (const CompositorClient *window : windows) {
        dispatch("hl.dsp.window.move({window='address:" + window->address + "', workspace=" + workspace_lua + ", follow=false})");
    }
}

void HyprlandCompositor::focus_workspace(int id, bool global) {
    dispatch(hypr_lua_focus(hypr_resolve_workspace(state_, id, global)));
}

void HyprlandCompositor::move_window(const std::string &address, int id, bool global) {
    dispatch(hypr_lua_move(address, hypr_resolve_workspace(state_, id, global), false));
}

void HyprlandCompositor::close_window(const std::string &address) { dispatch(hypr_lua_close(address)); }

void HyprlandCompositor::close_windows(CloseScope scope, int id) {
    std::vector<std::string> targets;
    for (const CompositorClient &client : state_.clients) {
        bool match = scope == CloseScope::all || (scope == CloseScope::workspace && client.workspace_id == id) ||
                     (scope == CloseScope::monitor && client.monitor_id == id);
        if (match) {
            targets.push_back(client.address);
        }
    }
    for (const std::string &address : targets) {
        dispatch(hypr_lua_close(address));
    }
    if (scope == CloseScope::all) {
        focus_workspace(1);
    }
}

void HyprlandCompositor::move_workspace_in(int id, bool global) {
    int destination = hypr_resolve_workspace(state_, id, global);
    auto it = state_.by_monitor.find(state_.focused_monitor);
    int source = it != state_.by_monitor.end() ? it->second.active_id : -1;
    if (source < 0 || source == destination) {
        return;
    }
    move_all(clients_in(source), std::to_string(destination));
    focus_workspace(id, global);
}

void HyprlandCompositor::swap_workspace(int id, bool global) {
    int destination = hypr_resolve_workspace(state_, id, global);
    auto it = state_.by_monitor.find(state_.focused_monitor);
    int source = it != state_.by_monitor.end() ? it->second.active_id : -1;
    if (source < 0 || source == destination) {
        return;
    }
    std::vector<const CompositorClient *> from_source = clients_in(source);
    std::vector<const CompositorClient *> from_destination = clients_in(destination);
    if (from_source.empty() && from_destination.empty()) {
        return;
    }
    std::string temporary = std::string("'") + swap_temp_workspace + "'";
    move_all(from_source, temporary);
    move_all(from_destination, std::to_string(source));
    move_all(from_source, std::to_string(destination));
    focus_workspace(id, global);
}

} // namespace astralia
