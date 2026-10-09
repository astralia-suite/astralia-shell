#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "core/log.h"

#include "service/i3_service.h"

namespace astralia {

namespace {

constexpr uint32_t i3_run_command = 0;
constexpr uint32_t i3_get_workspaces = 1;
constexpr uint32_t i3_subscribe = 2;
constexpr uint32_t i3_get_tree = 4;
constexpr uint32_t i3_event_workspace = 0;
constexpr std::size_t i3_header_size = 14;
constexpr std::string_view i3_magic = "i3-ipc";
constexpr timeval request_timeout{0, 250000};
constexpr const char *swap_temp_workspace = "__tmp_swp";

struct Box {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

struct Context {
    std::string output;
};

I3Window make_window(const Json &node, uint32_t workspace, const std::string &output, const Box &box, bool floating) {
    I3Window entry;
    entry.id = static_cast<int64_t>(node.number_or("id", 0));
    if (const Json *properties = node.find("window_properties")) {
        entry.window_class = properties->string_or("class");
    }
    if (entry.window_class.empty()) {
        entry.window_class = node.string_or("app_id");
    }
    entry.title = node.string_or("name");
    entry.x = box.x;
    entry.y = box.y;
    entry.width = box.width;
    entry.height = box.height;
    entry.workspace = workspace;
    entry.output = output;
    entry.floating = floating;
    entry.fullscreen = node.number_or("fullscreen_mode", 0) > 0;
    entry.focused = node.boolean_or("focused", false);
    return entry;
}

bool is_window(const Json &node) {
    const Json *window = node.find("window");
    if (window != nullptr && window->type == Json::Type::number) {
        return true;
    }
    const Json *app_id = node.find("app_id");
    return app_id != nullptr && app_id->type == Json::Type::string;
}

void place_tiled(const Json &node, const Box &box, const Box &area, uint32_t workspace, const std::string &output, I3Tree &tree) {
    if (is_window(node)) {
        bool fullscreen = node.number_or("fullscreen_mode", 0) > 0;
        tree.windows.push_back(make_window(node, workspace, output, fullscreen ? area : box, false));
        return;
    }
    const Json *children = node.find("nodes");
    if (children == nullptr || children->type != Json::Type::array || children->array.empty()) {
        return;
    }
    std::string layout = node.string_or("layout");
    double fallback = 1.0 / static_cast<double>(children->array.size());
    double total = 0.0;
    for (const Json &child : children->array) {
        total += child.number_or("percent", fallback);
    }
    double offset = 0.0;
    for (const Json &child : children->array) {
        double share = child.number_or("percent", fallback) / total;
        Box inner = box;
        if (layout == "splith") {
            inner.x = box.x + offset;
            inner.width = box.width * share;
            offset += inner.width;
        } else if (layout == "splitv") {
            inner.y = box.y + offset;
            inner.height = box.height * share;
            offset += inner.height;
        }
        place_tiled(child, inner, area, workspace, output, tree);
    }
}

void place_floating(const Json &node, double origin_x, double origin_y, const Box &area, uint32_t workspace, const std::string &output, I3Tree &tree) {
    const Json *rect = node.find("rect");
    if (is_window(node) && rect != nullptr) {
        bool fullscreen = node.number_or("fullscreen_mode", 0) > 0;
        Box box{rect->number_or("x", 0) - origin_x, rect->number_or("y", 0) - origin_y, rect->number_or("width", 0), rect->number_or("height", 0)};
        tree.windows.push_back(make_window(node, workspace, output, fullscreen ? area : box, true));
        return;
    }
    const Json *children = node.find("nodes");
    if (children == nullptr || children->type != Json::Type::array) {
        return;
    }
    for (const Json &child : children->array) {
        place_floating(child, origin_x, origin_y, area, workspace, output, tree);
    }
}

void collect(const Json &node, const Context &context, I3Tree &tree) {
    std::string type = node.string_or("type");
    Context inner = context;
    if (type == "output") {
        std::string name = node.string_or("name");
        if (name.starts_with("__")) {
            return;
        }
        inner.output = name;
        if (const Json *rect = node.find("rect")) {
            tree.outputs.push_back({name, rect->number_or("x", 0), rect->number_or("y", 0), rect->number_or("width", 0), rect->number_or("height", 0)});
        } else {
            tree.outputs.push_back({name, 0, 0, 0, 0});
        }
    }
    if (type == "workspace") {
        double number = node.number_or("num", -1);
        const Json *rect = node.find("rect");
        if (number < 1 || rect == nullptr) {
            return;
        }
        auto workspace = static_cast<uint32_t>(number);
        Box area{0.0, 0.0, rect->number_or("width", 0), rect->number_or("height", 0)};
        tree.workspaces.push_back({workspace, node.string_or("name"), context.output, rect->number_or("x", 0), rect->number_or("y", 0), area.width, area.height});
        place_tiled(node, area, area, workspace, context.output, tree);
        if (const Json *floating = node.find("floating_nodes"); floating != nullptr && floating->type == Json::Type::array) {
            for (const Json &child : floating->array) {
                place_floating(child, rect->number_or("x", 0), rect->number_or("y", 0), area, workspace, context.output, tree);
            }
        }
        return;
    }
    if (const Json *children = node.find("nodes"); children != nullptr && children->type == Json::Type::array) {
        for (const Json &child : children->array) {
            collect(child, inner, tree);
        }
    }
}

bool read_exact(int fd, char *buffer, std::size_t size) {
    while (size > 0) {
        ssize_t got = recv(fd, buffer, size, 0);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got <= 0) {
            return false;
        }
        buffer += got;
        size -= static_cast<std::size_t>(got);
    }
    return true;
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

} // namespace

I3Tree i3_parse_tree(const Json &root) {
    I3Tree tree;
    collect(root, {}, tree);
    return tree;
}

std::vector<I3WorkspaceStatus> i3_parse_workspaces(const Json &array) {
    std::vector<I3WorkspaceStatus> out;
    if (array.type != Json::Type::array) {
        return out;
    }
    for (const Json &entry : array.array) {
        double number = entry.number_or("num", -1);
        if (number < 1) {
            continue;
        }
        out.push_back({static_cast<uint32_t>(number), entry.string_or("output"), entry.boolean_or("visible", false), entry.boolean_or("focused", false)});
    }
    return out;
}

CompositorState i3_build_state(const I3Tree &tree, const std::vector<I3WorkspaceStatus> &statuses) {
    CompositorState state;
    int index = 0;
    for (const I3Output &output : tree.outputs) {
        CompositorMonitor monitor;
        monitor.id = index++;
        monitor.name = output.name;
        monitor.x = output.x;
        monitor.y = output.y;
        monitor.width = output.width;
        monitor.height = output.height;
        state.monitors.push_back(std::move(monitor));
        state.by_monitor[output.name];
    }
    for (CompositorMonitor &monitor : state.monitors) {
        for (const I3Workspace &workspace : tree.workspaces) {
            if (workspace.output == monitor.name && workspace.width > 0) {
                double left = workspace.x - monitor.x;
                double top = workspace.y - monitor.y;
                monitor.reserved = {left, top, monitor.width - left - workspace.width, monitor.height - top - workspace.height};
                break;
            }
        }
    }
    auto monitor_index = [&](const std::string &name) {
        for (const CompositorMonitor &monitor : state.monitors) {
            if (monitor.name == name) {
                return monitor.id;
            }
        }
        return -1;
    };
    for (const I3Workspace &workspace : tree.workspaces) {
        bool has_windows = std::ranges::any_of(tree.windows, [&](const I3Window &w) { return w.workspace == workspace.number; });
        state.by_monitor[workspace.output].workspaces.push_back({static_cast<int>(workspace.number), workspace.name, has_windows});
    }
    for (auto &[output, entry] : state.by_monitor) {
        std::ranges::sort(entry.workspaces, {}, &Workspace::id);
    }
    for (const I3WorkspaceStatus &status : statuses) {
        if (status.visible) {
            state.by_monitor[status.output].active_id = static_cast<int>(status.number);
        }
        if (status.focused) {
            state.focused_monitor = status.output;
        }
    }
    long history = 1;
    for (const I3Window &window : tree.windows) {
        CompositorClient client;
        client.address = std::to_string(window.id);
        client.window_class = window.window_class;
        client.title = window.title;
        client.workspace_id = static_cast<int>(window.workspace);
        client.monitor_id = monitor_index(window.output);
        double origin_x = 0.0;
        double origin_y = 0.0;
        for (const I3Workspace &workspace : tree.workspaces) {
            if (workspace.number == window.workspace) {
                origin_x = workspace.x;
                origin_y = workspace.y;
                break;
            }
        }
        client.at = {origin_x + window.x, origin_y + window.y};
        client.size = {window.width, window.height};
        client.floating = window.floating;
        client.fullscreen = window.fullscreen ? 1 : 0;
        client.focus_history_id = window.focused ? 0 : history++;
        state.clients.push_back(std::move(client));
    }
    return state;
}

std::string i3_encode(uint32_t type, std::string_view payload) {
    std::string message(i3_magic);
    uint32_t header[2] = {static_cast<uint32_t>(payload.size()), type};
    message.append(reinterpret_cast<const char *>(header), sizeof header);
    message.append(payload);
    return message;
}

std::size_t i3_decode(std::string_view buffer, std::vector<I3Message> &out) {
    std::size_t consumed = 0;
    while (buffer.size() - consumed >= i3_header_size) {
        std::string_view rest = buffer.substr(consumed);
        if (!rest.starts_with(i3_magic)) {
            return buffer.size();
        }
        uint32_t length = 0;
        uint32_t type = 0;
        std::memcpy(&length, rest.data() + i3_magic.size(), sizeof length);
        std::memcpy(&type, rest.data() + i3_magic.size() + sizeof length, sizeof type);
        if (rest.size() < i3_header_size + length) {
            break;
        }
        out.push_back({type, std::string(rest.substr(i3_header_size, length))});
        consumed += i3_header_size + length;
    }
    return consumed;
}

std::string i3_socket_path(std::string_view fallback) {
    for (const char *name : {"I3SOCK", "SWAYSOCK"}) {
        if (const char *env = std::getenv(name); env != nullptr && *env != '\0') {
            return env;
        }
    }
    return std::string(fallback);
}

bool I3Compositor::available(std::string_view fallback) { return !i3_socket_path(fallback).empty(); }

I3Compositor::I3Compositor(Reactor &loop, std::string socket_path) : loop_(loop), path_(std::move(socket_path)) {
    if (path_.empty()) {
        return;
    }
    refresh();
    UniqueFd fd = connect_to(path_);
    if (fd.get() < 0) {
        log::error("i3: cannot connect the event socket {}: {}", path_, std::strerror(errno));
        return;
    }
    std::string subscribe = i3_encode(i3_subscribe, R"(["workspace","window"])");
    char reply_header[i3_header_size];
    uint32_t length = 0;
    if (send(fd.get(), subscribe.data(), subscribe.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(subscribe.size()) ||
        !read_exact(fd.get(), reply_header, sizeof reply_header)) {
        log::error("i3: cannot subscribe to events");
        return;
    }
    std::memcpy(&length, reply_header + i3_magic.size(), sizeof length);
    std::string reply(length, '\0');
    if (!read_exact(fd.get(), reply.data(), reply.size())) {
        log::error("i3: cannot read the subscribe reply");
        return;
    }
    fcntl(fd.get(), F_SETFL, fcntl(fd.get(), F_GETFL, 0) | O_NONBLOCK);
    event_fd_ = std::move(fd);
    loop_.on_fd(event_fd_.get(), [this] { read_events(); });
}

I3Compositor::~I3Compositor() {
    if (event_fd_.get() >= 0) {
        loop_.remove_fd(event_fd_.get());
    }
}

std::optional<std::string> I3Compositor::request(uint32_t type, std::string_view payload) {
    UniqueFd fd = connect_to(path_);
    if (fd.get() < 0) {
        log::error("i3: cannot connect {}: {}", path_, std::strerror(errno));
        return std::nullopt;
    }
    std::string message = i3_encode(type, payload);
    if (send(fd.get(), message.data(), message.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(message.size())) {
        return std::nullopt;
    }
    char header[i3_header_size];
    if (!read_exact(fd.get(), header, sizeof header)) {
        return std::nullopt;
    }
    uint32_t length = 0;
    std::memcpy(&length, header + i3_magic.size(), sizeof length);
    std::string reply(length, '\0');
    if (!read_exact(fd.get(), reply.data(), reply.size())) {
        return std::nullopt;
    }
    return reply;
}

void I3Compositor::command(const std::string &text) { request(i3_run_command, text); }

void I3Compositor::refresh() {
    std::optional<std::string> tree_reply = request(i3_get_tree, {});
    std::optional<std::string> workspaces_reply = request(i3_get_workspaces, {});
    if (!tree_reply || !workspaces_reply) {
        return;
    }
    std::optional<Json> tree = parse_json(*tree_reply);
    std::optional<Json> workspaces = parse_json(*workspaces_reply);
    if (!tree || !workspaces) {
        return;
    }
    tree_ = i3_parse_tree(*tree);
    state_ = i3_build_state(tree_, i3_parse_workspaces(*workspaces));
}

bool I3Compositor::refresh_clients() {
    CompositorState before = state_;
    refresh();
    return before.clients != state_.clients;
}

void I3Compositor::refresh_active() {
    std::optional<std::string> reply = request(i3_get_workspaces, {});
    std::optional<Json> parsed = reply ? parse_json(*reply) : std::nullopt;
    if (!parsed) {
        return;
    }
    for (auto &[output, entry] : state_.by_monitor) {
        entry.active_id = -1;
    }
    for (const I3WorkspaceStatus &status : i3_parse_workspaces(*parsed)) {
        if (status.visible) {
            state_.by_monitor[status.output].active_id = static_cast<int>(status.number);
        }
        if (status.focused) {
            state_.focused_monitor = status.output;
        }
    }
}

void I3Compositor::read_events() {
    char buffer[4096];
    ssize_t n = 0;
    while ((n = recv(event_fd_.get(), buffer, sizeof buffer, MSG_DONTWAIT)) > 0) {
        event_buffer_.append(buffer, static_cast<std::size_t>(n));
    }
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        log::error("i3: the event socket closed");
        loop_.remove_fd(event_fd_.get());
        event_fd_ = UniqueFd();
        return;
    }
    std::vector<I3Message> messages;
    event_buffer_.erase(0, i3_decode(event_buffer_, messages));
    bool structural = false;
    bool active = false;
    for (const I3Message &message : messages) {
        if (!message.is_event()) {
            continue;
        }
        uint32_t kind = message.type & 0x7fffffffu;
        std::optional<Json> body = parse_json(message.payload);
        std::string change = body ? body->string_or("change") : std::string();
        if (kind == i3_event_workspace && change == "focus") {
            active = true;
        } else {
            structural = true;
        }
    }
    if (structural) {
        refresh();
        structure_changed.emit();
    } else if (active) {
        refresh_active();
        active_changed.emit();
    }
}

std::vector<const CompositorClient *> I3Compositor::clients_in(int workspace) const {
    std::vector<const CompositorClient *> out;
    for (const CompositorClient &client : state_.clients) {
        if (client.workspace_id == workspace) {
            out.push_back(&client);
        }
    }
    return out;
}

void I3Compositor::move_all(const std::vector<const CompositorClient *> &windows, const std::string &workspace) {
    for (const CompositorClient *window : windows) {
        command(std::format("[con_id={}] move container to workspace {}", window->address, workspace));
    }
}

void I3Compositor::focus_workspace(int id, bool) { command(std::format("workspace number {}", id)); }

void I3Compositor::move_window(const std::string &address, int id, bool) {
    command(std::format("[con_id={}] move container to workspace number {}", address, id));
}

void I3Compositor::close_window(const std::string &address) { command(std::format("[con_id={}] kill", address)); }

void I3Compositor::close_windows(CloseScope scope, int id) {
    std::vector<std::string> targets;
    for (const CompositorClient &client : state_.clients) {
        bool match = scope == CloseScope::all || (scope == CloseScope::workspace && client.workspace_id == id) ||
                     (scope == CloseScope::monitor && client.monitor_id == id);
        if (match) {
            targets.push_back(client.address);
        }
    }
    for (const std::string &address : targets) {
        command(std::format("[con_id={}] kill", address));
    }
    if (scope == CloseScope::all) {
        focus_workspace(1);
    }
}

void I3Compositor::move_workspace_in(int id, bool) {
    auto it = state_.by_monitor.find(state_.focused_monitor);
    int source = it != state_.by_monitor.end() ? it->second.active_id : -1;
    if (source < 0 || source == id) {
        return;
    }
    move_all(clients_in(source), std::format("number {}", id));
    focus_workspace(id);
}

void I3Compositor::swap_workspace(int id, bool) {
    auto it = state_.by_monitor.find(state_.focused_monitor);
    int source = it != state_.by_monitor.end() ? it->second.active_id : -1;
    if (source < 0 || source == id) {
        return;
    }
    std::vector<const CompositorClient *> from_source = clients_in(source);
    std::vector<const CompositorClient *> from_destination = clients_in(id);
    if (from_source.empty() && from_destination.empty()) {
        return;
    }
    move_all(from_source, swap_temp_workspace);
    move_all(from_destination, std::format("number {}", source));
    move_all(from_source, std::format("number {}", id));
    focus_workspace(id);
}

} // namespace astralia
