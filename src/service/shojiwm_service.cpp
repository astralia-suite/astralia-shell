#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "core/log.h"

#include "service/shojiwm_service.h"

namespace astralia {

namespace {

constexpr timeval request_timeout{0, 250000};
constexpr std::chrono::milliseconds reconnect_interval{2000};
constexpr std::size_t reply_limit = 4 * 1024 * 1024;

std::string environment_value(const char *name) {
    const char *value = std::getenv(name);
    return value != nullptr ? value : "";
}

UniqueFd connect_to(const std::string &path) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.empty() || path.size() >= sizeof addr.sun_path) {
        return {};
    }
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    UniqueFd fd(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (fd.get() < 0 || connect(fd.get(), reinterpret_cast<sockaddr *>(&addr), sizeof addr) < 0) {
        return {};
    }
    setsockopt(fd.get(), SOL_SOCKET, SO_RCVTIMEO, &request_timeout, sizeof request_timeout);
    setsockopt(fd.get(), SOL_SOCKET, SO_SNDTIMEO, &request_timeout, sizeof request_timeout);
    return fd;
}

bool send_all(int fd, std::string_view data) {
    while (!data.empty()) {
        ssize_t sent = send(fd, data.data(), data.size(), MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent <= 0) {
            return false;
        }
        data.remove_prefix(static_cast<std::size_t>(sent));
    }
    return true;
}

Json object_member(const Json &parent, std::string_view key) {
    const Json *member = parent.find(key);
    return member != nullptr && member->type == Json::Type::object ? *member : Json::make_object();
}

int ordinal_of(const CompositorState &state, const std::string &name) {
    const CompositorMonitor *monitor = compositor_monitor(state, name);
    return monitor != nullptr ? monitor->id : 0;
}

CompositorState without_focus(CompositorState state) {
    state.focused_monitor.clear();
    for (auto &[name, entry] : state.by_monitor) {
        entry.active_id = -1;
    }
    for (CompositorClient &client : state.clients) {
        client.focus_history_id = 0;
    }
    return state;
}

struct Ranked {
    std::size_t client = 0;
    bool active = false;
    double last_focused = 0.0;
};

std::array<double, 4> reserved_insets(const CompositorMonitor &monitor, const Json &entry) {
    const Json *usable = entry.find("usable");
    if (usable == nullptr || usable->type != Json::Type::object || monitor.width <= 0.0 ||
        monitor.height <= 0.0) {
        return {0.0, 0.0, 0.0, 0.0};
    }
    double x = usable->number_or("x", monitor.x);
    double y = usable->number_or("y", monitor.y);
    double width = usable->number_or("width", monitor.width);
    double height = usable->number_or("height", monitor.height);
    return {std::max(0.0, x - monitor.x), std::max(0.0, y - monitor.y),
            std::max(0.0, monitor.x + monitor.width - x - width),
            std::max(0.0, monitor.y + monitor.height - y - height)};
}

std::string hex_color(Color color, std::string_view alpha) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string out = "#";
    for (float channel : {color.r, color.g, color.b}) {
        int value = static_cast<int>(std::clamp(channel, 0.0f, 1.0f) * 255.0f + 0.5f);
        out += digits[static_cast<std::size_t>(value >> 4)];
        out += digits[static_cast<std::size_t>(value & 15)];
    }
    return out.append(alpha);
}

} // namespace

std::string shoji_socket_path(std::string_view runtime_dir, std::string_view wayland_display) {
    std::string path(runtime_dir.empty() ? std::string_view("/tmp") : runtime_dir);
    path += "/shojiwm-";
    path += wayland_display.empty() ? std::string_view("wayland-0") : wayland_display;
    path += ".sock";
    return path;
}

int shoji_per_monitor(const CompositorState &state) {
    return state.workspaces_per_monitor > 0 ? state.workspaces_per_monitor
                                            : shoji_default_workspaces_per_monitor;
}

int shoji_workspace_id(int per_monitor, int monitor_ordinal, int index) {
    return monitor_ordinal * per_monitor + index;
}

CompositorState shoji_parse_view(const Json &view) {
    CompositorState state;
    state.focused_monitor = view.string_or("currentMonitor");
    if (const Json *gaps = view.find("gapsOut");
        gaps != nullptr && gaps->type == Json::Type::number) {
        state.gaps_out_px = static_cast<int32_t>(gaps->number);
        state.hug_radius_px =
            *state.gaps_out_px + static_cast<int32_t>(view.number_or("rounding", 0.0));
    }
    if (int per = static_cast<int>(view.number_or("workspacesPerMonitor", 0.0)); per >= 1) {
        state.workspaces_per_monitor = per;
    }
    const int per_monitor = shoji_per_monitor(state);
    const Json *monitors = view.find("monitors");
    if (monitors == nullptr || monitors->type != Json::Type::array) {
        return state;
    }
    std::vector<Ranked> ranking;
    int ordinal = 0;
    for (const Json &entry : monitors->array) {
        CompositorMonitor monitor;
        monitor.id = ordinal;
        monitor.name = entry.string_or("name");
        Json rect = object_member(entry, "rect");
        monitor.x = rect.number_or("x", 0.0);
        monitor.y = rect.number_or("y", 0.0);
        monitor.width = rect.number_or("width", 0.0);
        monitor.height = rect.number_or("height", 0.0);
        monitor.reserved = reserved_insets(monitor, entry);
        MonitorWorkspaces &workspaces = state.by_monitor[monitor.name];
        int active = static_cast<int>(entry.number_or("active", 0.0));
        if (active >= 1 && active <= per_monitor) {
            workspaces.active_id = shoji_workspace_id(per_monitor, ordinal, active);
        }
        const Json *list = entry.find("workspaces");
        if (list != nullptr && list->type == Json::Type::array) {
            for (const Json &workspace : list->array) {
                int index = static_cast<int>(workspace.number_or("index", 0.0));
                if (index < 1 || index > per_monitor) {
                    continue;
                }
                int id = shoji_workspace_id(per_monitor, ordinal, index);
                workspaces.workspaces.push_back(
                    {id, std::to_string(index), workspace.number_or("windowCount", 0.0) > 0.0});
                const Json *windows = workspace.find("windows");
                if (windows == nullptr || windows->type != Json::Type::array) {
                    continue;
                }
                for (const Json &window : windows->array) {
                    CompositorClient client;
                    client.address = window.string_or("id");
                    client.window_class = window.string_or("appId");
                    client.title = window.string_or("title");
                    client.initial_title = client.window_class;
                    client.workspace_id = id;
                    client.monitor_id = ordinal;
                    Json position = object_member(window, "position");
                    Json size = object_member(window, "size");
                    client.at = {position.number_or("x", 0.0), position.number_or("y", 0.0)};
                    client.size = {size.number_or("width", 0.0), size.number_or("height", 0.0)};
                    ranking.push_back({state.clients.size(), window.boolean_or("active", false),
                                       window.number_or("lastFocusedAt", 0.0)});
                    state.clients.push_back(std::move(client));
                }
            }
        }
        std::ranges::sort(workspaces.workspaces, {}, &Workspace::id);
        state.monitors.push_back(std::move(monitor));
        ++ordinal;
    }
    std::ranges::stable_sort(ranking, [](const Ranked &a, const Ranked &b) {
        if (a.active != b.active) {
            return a.active;
        }
        return a.last_focused > b.last_focused;
    });
    long history = 0;
    for (const Ranked &rank : ranking) {
        state.clients[rank.client].focus_history_id = history++;
    }
    return state;
}

int shoji_resolve_workspace(const CompositorState &state, int id, bool global) {
    const int per_monitor = shoji_per_monitor(state);
    if (global || id < 1 || id > per_monitor) {
        return id;
    }
    return shoji_workspace_id(per_monitor, ordinal_of(state, state.focused_monitor), id);
}

std::optional<ShojiWorkspaceRef> shoji_workspace_ref(const CompositorState &state, int id) {
    if (id < 1) {
        return std::nullopt;
    }
    const int per_monitor = shoji_per_monitor(state);
    const CompositorMonitor *monitor = compositor_monitor(state, (id - 1) / per_monitor);
    if (monitor == nullptr) {
        return std::nullopt;
    }
    return ShojiWorkspaceRef{monitor->name, (id - 1) % per_monitor + 1};
}

std::string shoji_request(int id, std::string_view method, const Json &params) {
    Json message = Json::make_object();
    message.set("id", Json::from_number(id));
    message.set("method", Json::from_string(std::string(method)));
    if (params.type != Json::Type::null) {
        message.set("params", params);
    }
    return write_json(message) + "\n";
}

bool shoji_structure_differs(const CompositorState &a, const CompositorState &b) {
    return without_focus(a) != without_focus(b);
}

Json shoji_border_params(Color active, Color inactive) {
    Json params = Json::make_object();
    params.set("active", Json::from_string(hex_color(active, "ee")));
    params.set("inactive", Json::from_string(hex_color(inactive, "cc")));
    return params;
}

std::string ShojiCompositor::default_socket_path() {
    return shoji_socket_path(environment_value("XDG_RUNTIME_DIR"),
                             environment_value("WAYLAND_DISPLAY"));
}

bool ShojiCompositor::available() {
    std::string desktop = environment_value("XDG_CURRENT_DESKTOP");
    return desktop.find("ShojiWM") != std::string::npos ||
           access(default_socket_path().c_str(), F_OK) == 0;
}

ShojiCompositor::ShojiCompositor(Reactor &loop, std::string socket_path)
    : loop_(loop), path_(std::move(socket_path)) {
    refresh();
    connect_events();
    loop_.add_timer([] { return reconnect_interval; },
                    [this] {
                        if (event_fd_.get() >= 0) {
                            return;
                        }
                        connect_events();
                        if (event_fd_.get() >= 0) {
                            refresh();
                            structure_changed.emit();
                            if (border_params_) {
                                call("theme.borders", *border_params_);
                            }
                        }
                    });
}

ShojiCompositor::~ShojiCompositor() {
    if (event_fd_.get() >= 0) {
        loop_.remove_fd(event_fd_.get());
    }
}

void ShojiCompositor::connect_events() {
    UniqueFd fd = connect_to(path_);
    if (fd.get() < 0) {
        return;
    }
    fcntl(fd.get(), F_SETFL, fcntl(fd.get(), F_GETFL, 0) | O_NONBLOCK);
    event_buffer_.clear();
    event_fd_ = std::move(fd);
    loop_.on_fd(event_fd_.get(), [this] { read_events(); });
    log::info("ShojiWM: listening on {}", path_);
}

std::optional<Json> ShojiCompositor::call(std::string_view method, const Json &params) {
    UniqueFd fd = connect_to(path_);
    if (fd.get() < 0) {
        log::error("ShojiWM: cannot connect {}: {}", path_, std::strerror(errno));
        return std::nullopt;
    }
    int id = next_id_++;
    if (!send_all(fd.get(), shoji_request(id, method, params))) {
        log::error("ShojiWM: cannot send {}", method);
        return std::nullopt;
    }
    std::string buffer;
    char chunk[4096];
    while (buffer.size() < reply_limit) {
        std::size_t end = buffer.find('\n');
        if (end == std::string::npos) {
            ssize_t got = recv(fd.get(), chunk, sizeof chunk, 0);
            if (got < 0 && errno == EINTR) {
                continue;
            }
            if (got <= 0) {
                break;
            }
            buffer.append(chunk, static_cast<std::size_t>(got));
            continue;
        }
        std::optional<Json> line = parse_json(std::string_view(buffer).substr(0, end));
        buffer.erase(0, end + 1);
        if (!line || line->number_or("id", -1.0) != id) {
            continue;
        }
        if (const Json *error = line->find("error")) {
            log::error("ShojiWM: {} failed: {}", method, error->string);
            return std::nullopt;
        }
        const Json *result = line->find("result");
        return result != nullptr ? *result : Json{};
    }
    log::error("ShojiWM: no reply to {}", method);
    return std::nullopt;
}

void ShojiCompositor::refresh() {
    std::optional<Json> view = call("workspaces.get", Json{});
    if (view && view->type == Json::Type::object) {
        state_ = shoji_parse_view(*view);
    }
}

bool ShojiCompositor::refresh_clients() {
    std::vector<CompositorClient> before = state_.clients;
    refresh();
    return before != state_.clients;
}

void ShojiCompositor::apply(CompositorState next) {
    if (next == state_) {
        return;
    }
    bool structural = shoji_structure_differs(state_, next);
    state_ = std::move(next);
    if (structural) {
        structure_changed.emit();
    } else {
        active_changed.emit();
    }
}

void ShojiCompositor::read_events() {
    char buffer[4096];
    ssize_t n = 0;
    while ((n = recv(event_fd_.get(), buffer, sizeof buffer, MSG_DONTWAIT)) > 0) {
        event_buffer_.append(buffer, static_cast<std::size_t>(n));
    }
    bool closed = n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK);
    std::optional<CompositorState> latest;
    std::size_t end = 0;
    while ((end = event_buffer_.find('\n')) != std::string::npos) {
        std::optional<Json> line = parse_json(std::string_view(event_buffer_).substr(0, end));
        event_buffer_.erase(0, end + 1);
        if (!line || line->string_or("event") != "workspaces.changed") {
            continue;
        }
        if (const Json *payload = line->find("payload");
            payload != nullptr && payload->type == Json::Type::object) {
            latest = shoji_parse_view(*payload);
        }
    }
    if (latest) {
        apply(std::move(*latest));
    }
    if (closed) {
        log::info("ShojiWM: the event socket closed");
        loop_.remove_fd(event_fd_.get());
        event_fd_ = UniqueFd();
        event_buffer_.clear();
    }
}

void ShojiCompositor::workspace_call(std::string_view method, int id, bool global) {
    std::optional<ShojiWorkspaceRef> ref =
        shoji_workspace_ref(state_, shoji_resolve_workspace(state_, id, global));
    if (!ref) {
        return;
    }
    Json params = Json::make_object();
    params.set("monitor", Json::from_string(ref->monitor));
    params.set("index", Json::from_number(ref->index));
    call(method, params);
}

void ShojiCompositor::focus_workspace(int id, bool global) {
    workspace_call("workspaces.activate", id, global);
}

void ShojiCompositor::move_window(const std::string &address, int id, bool global) {
    std::optional<ShojiWorkspaceRef> ref =
        shoji_workspace_ref(state_, shoji_resolve_workspace(state_, id, global));
    if (!ref) {
        return;
    }
    Json params = Json::make_object();
    params.set("windowId", Json::from_string(address));
    params.set("monitor", Json::from_string(ref->monitor));
    params.set("index", Json::from_number(ref->index));
    params.set("follow", Json::from_bool(false));
    call("windows.moveTo", params);
}

void ShojiCompositor::close_window(const std::string &address) {
    Json params = Json::make_object();
    params.set("windowId", Json::from_string(address));
    call("windows.close", params);
}

void ShojiCompositor::close_windows(CloseScope scope, int id) {
    if (scope == CloseScope::all) {
        call("windows.closeAll", Json{});
        return;
    }
    if (scope == CloseScope::workspace) {
        workspace_call("workspaces.clear", id, true);
        return;
    }
    std::vector<std::string> targets;
    for (const CompositorClient &client : state_.clients) {
        if (client.monitor_id == id) {
            targets.push_back(client.address);
        }
    }
    for (const std::string &address : targets) {
        close_window(address);
    }
}

void ShojiCompositor::move_workspace_in(int id, bool global) {
    workspace_call("workspaces.moveInto", id, global);
}

void ShojiCompositor::swap_workspace(int id, bool global) {
    workspace_call("workspaces.swap", id, global);
}

void ShojiCompositor::apply_border_colors(Color active, Color inactive) {
    border_params_ = shoji_border_params(active, inactive);
    call("theme.borders", *border_params_);
}

} // namespace astralia
