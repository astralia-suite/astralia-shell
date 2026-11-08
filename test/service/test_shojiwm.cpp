#include <array>
#include <cstring>
#include <filesystem>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "core/json.h"
#include "core/poll_reactor.h"

#include "service/shojiwm_service.h"

#include "check.h"
#include "core/pump.h"

namespace {

constexpr const char *two_monitors = R"({
  "currentMonitor": "DP-2",
  "workspacesPerMonitor": 10,
  "monitors": [
    {"name": "DP-1", "active": 2, "rect": {"x": 0, "y": 0, "width": 1920, "height": 1080},
     "usable": {"x": 10, "y": 40, "width": 1900, "height": 990},
     "workspaces": [
       {"index": 1, "windowCount": 1, "active": false, "windows": [
         {"id": "w1", "appId": "kitty", "title": "shell", "active": false, "lastFocusedAt": 100,
          "position": {"x": 10, "y": 20}, "size": {"width": 800, "height": 600}}]},
       {"index": 2, "windowCount": 0, "active": true, "windows": []},
       {"index": 11, "windowCount": 1, "active": false, "windows": [
         {"id": "w9", "appId": "stray", "title": "", "lastFocusedAt": 1}]}]},
    {"name": "DP-2", "active": 1, "usable": {"x": 0, "y": 30, "width": 800, "height": 600},
     "workspaces": [
       {"index": 1, "windowCount": 2, "active": true, "windows": [
         {"id": "w2", "appId": "firefox", "title": "web", "active": true, "lastFocusedAt": 50,
          "position": {"x": 1930, "y": 5}, "size": {"width": 1000, "height": 700}},
         {"id": "w3", "appId": "foot", "title": "term", "active": false, "lastFocusedAt": 300}]}]}
  ]})";

std::string flatten(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (c != '\n') {
            out.push_back(c);
        }
    }
    return out;
}

std::string read_line(int fd) {
    std::string line;
    char c = 0;
    while (recv(fd, &c, 1, 0) == 1 && c != '\n') {
        line.push_back(c);
    }
    return line;
}

class FakeShoji {
  public:
    FakeShoji(std::string path, std::vector<std::string> &requests)
        : requests_(requests), path_(std::move(path)) {
        unlink(path_.c_str());
        listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);
        bound_ = bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof addr) == 0 &&
                 listen(listen_fd_, 4) == 0;
        thread_ = std::thread([this] { serve(); });
    }

    ~FakeShoji() {
        thread_.join();
        close(listen_fd_);
        unlink(path_.c_str());
    }

    bool bound() const { return bound_; }

  private:
    void answer(int fd) {
        std::string line = read_line(fd);
        requests_.push_back(line);
        auto request = astralia::parse_json(line);
        int id = request ? static_cast<int>(request->number_or("id", 0)) : 0;
        std::string reply;
        if (request && request->string_or("method") == "workspaces.get") {
            reply = "{\"event\":\"dock.proximity\",\"payload\":{}}\n{\"id\":" + std::to_string(id) +
                    ",\"result\":" + flatten(two_monitors) + "}\n";
        } else {
            reply = "{\"id\":" + std::to_string(id) + "}\n";
        }
        (void)!write(fd, reply.data(), reply.size());
        close(fd);
    }

    void serve() {
        answer(accept(listen_fd_, nullptr, nullptr));
        int events = accept(listen_fd_, nullptr, nullptr);
        std::string changed =
            R"({"event":"workspaces.changed","payload":{"currentMonitor":"DP-1","monitors":[{"name":"DP-1","active":1,"workspaces":[{"index":1,"windowCount":0,"active":true,"windows":[]}]}]}})";
        changed += "\n";
        (void)!write(events, changed.data(), changed.size());
        answer(accept(listen_fd_, nullptr, nullptr));
        close(events);
    }

    std::vector<std::string> &requests_;
    std::string path_;
    int listen_fd_ = -1;
    bool bound_ = false;
    std::thread thread_;
};

} // namespace

void check_shojiwm() {
    using namespace astralia;
    using test::check;

    check(shoji_socket_path("/run/user/1000", "wayland-1") ==
              "/run/user/1000/shojiwm-wayland-1.sock",
          "shoji socket path follows the runtime dir and display");
    check(shoji_socket_path("", "") == "/tmp/shojiwm-wayland-0.sock",
          "shoji socket path falls back like defaultSocketPath");

    auto view = parse_json(two_monitors);
    check(view.has_value(), "shoji view parses");
    if (!view) {
        return;
    }
    CompositorState state = shoji_parse_view(*view);
    check(state.focused_monitor == "DP-2", "focused monitor comes from currentMonitor");
    check(state.monitors.size() == 2 && state.monitors[0].id == 0 && state.monitors[1].id == 1,
          "monitors keep their view order as ids");
    check(state.monitors[0].width == 1920 && state.monitors[0].scale == 1.0,
          "monitor rect is taken as logical size");
    check(state.monitors[1].width == 0.0, "a missing rect leaves a zero monitor");
    check(state.monitors[0].reserved == std::array<double, 4>{10.0, 40.0, 10.0, 50.0},
          "reserved insets are the rect minus the usable area");
    WorkArea area = compositor_work_area(state.monitors[0]);
    check(area.x == 10.0 && area.y == 40.0 && area.width == 1900.0 && area.height == 990.0,
          "the work area matches the usable area");
    check(state.monitors[1].reserved == std::array<double, 4>{0.0, 0.0, 0.0, 0.0},
          "a usable area without a rect reserves nothing");
    auto bare = parse_json(R"({"monitors":[{"name":"X","rect":{"width":100,"height":100}}]})");
    check(bare && shoji_parse_view(*bare).monitors[0].reserved ==
                      std::array<double, 4>{0.0, 0.0, 0.0, 0.0},
          "a view without a usable area reserves nothing");
    check(bare &&
              shoji_per_monitor(shoji_parse_view(*bare)) == shoji_default_workspaces_per_monitor,
          "a view without workspacesPerMonitor uses the default");

    Json borders = shoji_border_params({1.0f, 0.5f, 0.0f, 1.0f}, {0.0f, 0.0f, 2.0f, 1.0f});
    check(borders.string_or("active") == "#ff8000ee" &&
              borders.string_or("inactive") == "#0000ffcc",
          "border params are hex with the hyprland alphas, channels clamped");
    const MonitorWorkspaces &first = state.by_monitor["DP-1"];
    check(first.workspaces.size() == 2 && first.workspaces[0].id == 1 &&
              first.workspaces[1].id == 2 && first.workspaces[1].name == "2",
          "workspaces past the per-monitor limit are dropped");
    check(first.workspaces[0].occupied && !first.workspaces[1].occupied,
          "occupied follows windowCount");
    check(first.active_id == 2, "an empty active workspace stays active");
    const MonitorWorkspaces &second = state.by_monitor["DP-2"];
    check(second.workspaces.size() == 1 && second.workspaces[0].id == 11 &&
              second.workspaces[0].name == "1",
          "the second monitor's index 1 maps to id 11 and keeps its label");
    check(second.active_id == 11, "second monitor active id is offset");
    check(state.clients.size() == 3, "windows of dropped workspaces are skipped");
    auto client = [&](const std::string &address) -> const CompositorClient * {
        for (const CompositorClient &c : state.clients) {
            if (c.address == address) {
                return &c;
            }
        }
        return nullptr;
    };
    const CompositorClient *kitty = client("w1");
    const CompositorClient *firefox = client("w2");
    const CompositorClient *foot = client("w3");
    check(kitty != nullptr && firefox != nullptr && foot != nullptr, "every window is a client");
    if (kitty != nullptr && firefox != nullptr && foot != nullptr) {
        check(kitty->window_class == "kitty" && kitty->workspace_id == 1 && kitty->monitor_id == 0,
              "client fields map from the window");
        check(kitty->at[0] == 10 && kitty->at[1] == 20 && kitty->size[0] == 800,
              "client geometry maps from position and size");
        check(firefox->workspace_id == 11 && firefox->monitor_id == 1,
              "client on the second monitor gets the offset id");
        check(firefox->focus_history_id == 0 && foot->focus_history_id == 1 &&
                  kitty->focus_history_id == 2,
              "the active window leads, then most recent focus");
    }

    check(state.workspaces_per_monitor == 10 && shoji_per_monitor(state) == 10,
          "the per-monitor count comes from the view");
    check(shoji_workspace_id(10, 1, 3) == 13, "workspace id offsets by monitor");
    check(shoji_resolve_workspace(state, 3, false) == 13,
          "a bare index resolves on the focused monitor");
    check(shoji_resolve_workspace(state, 3, true) == 3, "a global id stays");
    check(shoji_resolve_workspace(state, 13, false) == 13, "an id past one monitor stays");
    auto ref = shoji_workspace_ref(state, 13);
    check(ref && ref->monitor == "DP-2" && ref->index == 3, "id 13 is DP-2 index 3");
    ref = shoji_workspace_ref(state, 10);
    check(ref && ref->monitor == "DP-1" && ref->index == 10, "id 10 is DP-1 index 10");
    check(!shoji_workspace_ref(state, 21), "an id past the last monitor has no ref");
    check(!shoji_workspace_ref(state, 0), "id 0 has no ref");

    check(shoji_request(4, "workspaces.get", Json{}) ==
              "{\"id\":4,\"method\":\"workspaces.get\"}\n",
          "a request without params omits them");
    Json params = Json::make_object();
    params.set("monitor", Json::from_string("DP-1"));
    auto encoded = parse_json(shoji_request(5, "workspaces.activate", params));
    check(encoded && encoded->string_or("method") == "workspaces.activate" &&
              encoded->find("params") != nullptr &&
              encoded->find("params")->string_or("monitor") == "DP-1",
          "a request carries its params");

    CompositorState refocused = state;
    refocused.focused_monitor = "DP-1";
    refocused.by_monitor["DP-1"].active_id = 1;
    refocused.clients[0].focus_history_id = 7;
    check(!shoji_structure_differs(state, refocused), "focus changes are not structural");
    refocused.clients.pop_back();
    check(shoji_structure_differs(state, refocused), "a closed window is structural");

    std::string path = (std::filesystem::temp_directory_path() /
                        ("astralia-test-shoji-" + std::to_string(getpid()) + ".sock"))
                           .string();
    std::vector<std::string> requests;
    {
        FakeShoji server(path, requests);
        check(server.bound(), "fake shoji server binds");
        auto made = PollReactor::create();
        check(made.has_value(), "reactor is created");
        if (!made) {
            return;
        }
        PollReactor &reactor = *made;
        ShojiCompositor compositor(reactor, path);
        check(compositor.state().monitors.size() == 2,
              "workspaces.get fills the state past a broadcast line");
        int structure = 0;
        compositor.structure_changed.connect([&] { ++structure; });
        check(test::pump_until(reactor, [&] { return structure > 0; }),
              "a workspaces.changed broadcast arrives");
        check(compositor.state().monitors.size() == 1 &&
                  compositor.state().focused_monitor == "DP-1",
              "the broadcast replaces the state");
        compositor.focus_workspace(1, true);
    }
    check(requests.size() == 2, "the fake server saw two requests");
    if (requests.size() == 2) {
        auto get = parse_json(requests[0]);
        auto activate = parse_json(requests[1]);
        check(get && get->string_or("method") == "workspaces.get",
              "first request is workspaces.get");
        const Json *sent = activate ? activate->find("params") : nullptr;
        check(activate && activate->string_or("method") == "workspaces.activate" &&
                  sent != nullptr && sent->string_or("monitor") == "DP-1" &&
                  sent->number_or("index", 0) == 1,
              "focus_workspace sends workspaces.activate with monitor and index");
    }
}
