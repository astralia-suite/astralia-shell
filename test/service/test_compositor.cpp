#include <chrono>
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

#include "service/dock_service.h"
#include "service/hyprland_service.h"
#include "service/i3_service.h"

#include "check.h"

namespace {

class OneShotServer {
  public:
    OneShotServer(const std::string &path, const char *reply, int hold_ms) {
        unlink(path.c_str());
        listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        bound_ = bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof addr) == 0 && listen(listen_fd_, 1) == 0;
        thread_ = std::thread([this, reply, hold_ms] {
            int fd = accept(listen_fd_, nullptr, nullptr);
            char buffer[64];
            while (read(fd, buffer, sizeof buffer) > 0) {
            }
            if (reply != nullptr) {
                (void)!write(fd, reply, std::strlen(reply));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(hold_ms));
            close(fd);
        });
    }

    ~OneShotServer() {
        thread_.join();
        close(listen_fd_);
    }

    bool bound() const { return bound_; }

  private:
    int listen_fd_ = -1;
    bool bound_ = false;
    std::thread thread_;
};

std::string socket_path() {
    return (std::filesystem::temp_directory_path() / ("astralia-test-hypr-" + std::to_string(getpid()) + ".sock")).string();
}

constexpr const char *one_client = R"([{"address": "0x1", "class": "kitty", "title": "t", "workspace": {"id": 1}, "monitor": 0, "at": [10, 0]}])";

} // namespace

void check_dock() {
    using namespace astralia;
    using test::check;
    CompositorState state;
    state.by_monitor["DP-1"].active_id = 2;
    state.by_monitor["DP-2"].active_id = 5;
    CompositorClient a;
    a.address = "0xaaa";
    a.window_class = "firefox";
    a.workspace_id = 2;
    a.at = {300.0, 0.0};
    a.focus_history_id = 1;
    CompositorClient b;
    b.address = "0xbbb";
    b.window_class = "kitty";
    b.workspace_id = 2;
    b.at = {100.0, 0.0};
    b.focus_history_id = 0;
    CompositorClient c;
    c.address = "0xccc";
    c.window_class = "mpv";
    c.workspace_id = 1;
    c.focus_history_id = 2;
    state.clients = {a, b, c};

    std::vector<DockEntry> entries = dock_entries_for_monitor(state, "DP-1");
    check(entries.size() == 2 && entries[0].address == "0xbbb" && entries[0].window_class == "kitty" && entries[0].focused, "dock lists the active workspace left to right, focused first here");
    check(entries[1].address == "0xaaa" && !entries[1].focused, "second dock entry");
    check(dock_entries_for_monitor(state, "DP-2").empty() && dock_entries_for_monitor(state, "HDMI-1").empty(), "empty workspace and unknown output give nothing");
    state.clients[0].at = {50.0, 0.0};
    std::vector<DockEntry> reordered = dock_entries_for_monitor(state, "DP-1");
    check(reordered.size() == 2 && reordered[0].address == "0xaaa" && reordered[1].address == "0xbbb", "dock follows window positions");
}

void check_hyprland() {
    using namespace astralia;
    using test::check;
    {
        std::vector<CompositorClient> clients = hypr_parse_clients(R"([{"address":"0x1","class":"kitty","title":"t","workspace":{"id":3,"name":"3"},"monitor":1,"at":[10,20],"size":[300,200],"floating":true,"fullscreen":2,"pinned":true,"focusHistoryID":4,"xwayland":true},{"address":"0x2"}])");
        check(clients.size() == 2, "two clients parse");
        check(clients[0].address == "0x1" && clients[0].window_class == "kitty" && clients[0].workspace_id == 3 && clients[0].monitor_id == 1, "client identity");
        check(clients[0].at[0] == 10 && clients[0].at[1] == 20 && clients[0].size[0] == 300 && clients[0].size[1] == 200, "client geometry");
        check(clients[0].floating && clients[0].fullscreen == 2 && clients[0].pinned && clients[0].focus_history_id == 4 && clients[0].xwayland, "client flags");
        check(clients[1].workspace_id == -1 && clients[1].size[0] == 100, "missing fields take the defaults");
        check(hypr_parse_clients("garbage").empty() && hypr_parse_clients("{}").empty(), "invalid replies give no clients");
        std::vector<CompositorClient> moved = clients;
        check(!hypr_client_order_differs(clients, moved), "identical lists do not differ");
        moved[0].at[0] = 99;
        check(hypr_client_order_differs(clients, moved), "a moved window differs");
        moved = clients;
        moved.pop_back();
        check(hypr_client_order_differs(clients, moved), "a different count differs");
    }
    {
        CompositorState state;
        hypr_parse_workspaces(R"([{"id":2,"name":"2","monitor":"DP-1","windows":0},{"id":1,"name":"1","monitor":"DP-1","windows":3},{"id":-99,"name":"special","monitor":"DP-1","windows":1},{"id":11,"name":"11","monitor":"HDMI-1","windows":1}])", state);
        check(state.by_monitor["DP-1"].workspaces.size() == 2 && state.by_monitor["DP-1"].workspaces[0].id == 1 && state.by_monitor["DP-1"].workspaces[0].occupied && !state.by_monitor["DP-1"].workspaces[1].occupied, "workspaces are sorted and the special one is dropped");
        check(state.by_monitor["HDMI-1"].workspaces.size() == 1, "workspaces group by monitor");
        hypr_parse_monitors(R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":1920,"height":1080,"scale":1.5,"transform":1,"focused":true,"activeWorkspace":{"id":2},"reserved":[0,40,0,0]},{"id":1,"name":"HDMI-1","width":1280,"height":720,"activeWorkspace":{"id":11}}])", state);
        check(state.monitors.size() == 2 && state.monitors[0].scale == 1.5 && state.monitors[0].transform == 1 && state.monitors[0].reserved[1] == 40, "monitors parse");
        check(state.focused_monitor == "DP-1" && state.by_monitor["DP-1"].active_id == 2 && state.by_monitor["HDMI-1"].active_id == 11, "focus and active workspaces");
    }
    {
        CompositorState state;
        check(hypr_apply_event(state, "garbage") == HyprEventKind::none, "an event without a separator is ignored");
        check(hypr_apply_event(state, "focusedmonv2>>DP-1,4") == HyprEventKind::active && state.focused_monitor == "DP-1" && state.by_monitor["DP-1"].active_id == 4, "focused monitor event");
        check(hypr_apply_event(state, "workspacev2>>7,seven") == HyprEventKind::active && state.by_monitor["DP-1"].active_id == 7, "workspace event updates the focused monitor");
        check(hypr_apply_event(state, "openwindow>>0x1,1,kitty,t") == HyprEventKind::structural, "openwindow is structural");
        check(hypr_apply_event(state, "activewindowv2>>0x1") == HyprEventKind::structural, "focus change is structural");
        check(hypr_apply_event(state, "submap>>x") == HyprEventKind::none, "unrelated events are ignored");
        CompositorState empty;
        check(hypr_apply_event(empty, "workspacev2>>3,three") == HyprEventKind::none, "a workspace event without a focused monitor is ignored");
    }
    {
        CompositorState state;
        state.focused_monitor = "DP-1";
        state.by_monitor["DP-1"].active_id = 14;
        check(hypr_resolve_workspace(state, 3, false) == 13, "workspace ids resolve into the focused monitor's page");
        check(hypr_resolve_workspace(state, 3, true) == 3, "global ids are kept");
        check(hypr_resolve_workspace(state, 25, false) == 25 && hypr_resolve_workspace(state, 0, false) == 0, "ids outside a page are kept");
        CompositorState fresh;
        check(hypr_resolve_workspace(fresh, 4, false) == 4, "without an active workspace the first page is used");
        check(hypr_lua_focus(5) == "hl.dsp.focus({workspace=5})", "focus command");
        check(hypr_lua_move("0x9", 3, true) == "hl.dsp.window.move({window='address:0x9', workspace=3, follow=true})", "move command");
        check(hypr_lua_move("", 3, false) == "hl.dsp.window.move({window='activewindow', workspace=3, follow=false})", "move command without an address targets the active window");
        check(hypr_lua_close("0x9") == "hl.dsp.window.close({window='address:0x9'})", "close command");
    }
    check(hypr_parse_gaps_out(R"({"option":"general:gaps_out","css":"20 20 20 20","set":true})") == 20 && hypr_parse_gaps_out("x") == 0 && hypr_parse_gaps_out("{}") == 0, "gaps_out parses");
    check(hypr_parse_rounding(R"({"option":"decoration:rounding","int":10,"set":true})") == 10 && hypr_parse_rounding("x") == 0, "rounding parses");

    auto created = PollReactor::create();
    check(created.has_value(), "reactor");
    if (!created) {
        return;
    }
    PollReactor reactor = std::move(*created);
    std::string path = socket_path();
    unsetenv("HYPRLAND_INSTANCE_SIGNATURE");
    {
        HyprlandCompositor compositor(reactor);
        compositor.set_request_socket(path);
        OneShotServer server(path, nullptr, 400);
        check(server.bound(), "test server binds");
        auto start = std::chrono::steady_clock::now();
        bool changed = compositor.refresh_clients();
        auto elapsed = std::chrono::steady_clock::now() - start;
        check(!changed && elapsed < std::chrono::milliseconds(300), "a stalled compositor times out and reads as no change");
        check(compositor.state().clients.empty(), "a timeout keeps the previous clients");
    }
    {
        HyprlandCompositor compositor(reactor);
        compositor.set_request_socket(path);
        OneShotServer server(path, one_client, 0);
        check(compositor.refresh_clients(), "a new client list is a change");
        check(compositor.state().clients.size() == 1 && compositor.state().clients[0].address == "0x1", "clients are stored");
    }
    unlink(path.c_str());
}

void check_i3() {
    using namespace astralia;
    using test::check;
    auto root = parse_json(R"({
      "type": "root", "nodes": [
        {"type": "output", "name": "__i3", "nodes": [{"type": "con", "nodes": [
          {"type": "workspace", "num": -1, "rect": {"x": 0, "y": 0, "width": 10, "height": 10}, "nodes": [
            {"id": 9, "window": 99, "rect": {"x": 0, "y": 0, "width": 5, "height": 5}}]}]}]},
        {"type": "output", "name": "LVDS1", "rect": {"x": 100, "y": 50, "width": 1000, "height": 820}, "nodes": [{"type": "con", "nodes": [
          {"type": "workspace", "num": 2, "name": "2", "layout": "splith", "rect": {"x": 100, "y": 70, "width": 1000, "height": 800},
           "nodes": [
             {"id": 5, "window": 50, "percent": 0.25, "name": "page", "rect": {"x": 9, "y": 9, "width": 9, "height": 9},
              "window_properties": {"class": "Firefox"}},
             {"id": 7, "layout": "splitv", "percent": 0.75, "nodes": [
               {"id": 8, "window": 80, "percent": 0.5, "focused": true, "rect": {"x": 1, "y": 1, "width": 1, "height": 1},
                "window_properties": {"class": "foot"}},
               {"id": 10, "window": 100, "percent": 0.5, "fullscreen_mode": 1, "rect": {"x": 1, "y": 1, "width": 1, "height": 1},
                "window_properties": {"class": "vlc"}}]}],
           "floating_nodes": [{"type": "floating_con", "rect": {"x": 200, "y": 190, "width": 300, "height": 200},
                               "nodes": [{"id": 6, "window": 60, "floating": "user_on",
                                          "rect": {"x": 200, "y": 190, "width": 300, "height": 200},
                                          "window_properties": {"class": "mpv"}}]}]}]}]}
      ]})");
    check(root.has_value(), "tree parses");
    if (!root) {
        return;
    }
    I3Tree tree = i3_parse_tree(*root);
    check(tree.outputs.size() == 1 && tree.outputs[0].name == "LVDS1" && tree.outputs[0].x == 100 && tree.outputs[0].width == 1000, "the internal output is skipped");
    check(tree.workspaces.size() == 1 && tree.workspaces[0].number == 2 && tree.workspaces[0].output == "LVDS1" && tree.workspaces[0].width == 1000 && tree.workspaces[0].height == 800 && tree.workspaces[0].x == 100 && tree.workspaces[0].y == 70, "scratchpad skipped, workspace size and origin kept");
    check(tree.windows.size() == 4, "scratchpad windows skipped");
    if (tree.windows.size() != 4) {
        return;
    }
    const I3Window &first = tree.windows[0];
    check(first.id == 5 && first.window_class == "Firefox" && first.title == "page" && first.workspace == 2 && first.x == 0 && first.y == 0 && first.width == 250 && first.height == 800 && !first.floating && !first.fullscreen, "tiled window takes its percent, ignoring the stale rect");
    const I3Window &nested = tree.windows[1];
    check(nested.id == 8 && nested.x == 250 && nested.y == 0 && nested.width == 750 && nested.height == 400, "nested split divides the remaining box");
    const I3Window &fullscreen = tree.windows[2];
    check(fullscreen.id == 10 && fullscreen.fullscreen && fullscreen.x == 0 && fullscreen.y == 0 && fullscreen.width == 1000 && fullscreen.height == 800, "fullscreen window fills the workspace");
    const I3Window &floating = tree.windows[3];
    check(floating.id == 6 && floating.floating && floating.x == 100 && floating.y == 120 && floating.width == 300, "floating window keeps its rect relative to the workspace");
    check(nested.focused && !first.focused && !fullscreen.focused && !floating.focused, "focused flag parsed");

    auto workspaces = parse_json(R"([{"num":2,"name":"2","output":"LVDS1","visible":true,"focused":true},{"num":-1,"name":"x","output":"LVDS1","visible":false,"focused":false},{"num":5,"name":"5","output":"HDMI1","visible":false,"focused":false}])");
    check(workspaces.has_value(), "workspaces parse");
    if (!workspaces) {
        return;
    }
    std::vector<I3WorkspaceStatus> statuses = i3_parse_workspaces(*workspaces);
    check(statuses.size() == 2 && statuses[0].visible && statuses[0].focused && statuses[1].number == 5, "named workspaces are dropped");
    CompositorState state = i3_build_state(tree, statuses);
    check(state.monitors.size() == 1 && state.monitors[0].name == "LVDS1" && state.monitors[0].x == 100, "monitors come from the tree outputs");
    check(state.focused_monitor == "LVDS1" && state.by_monitor["LVDS1"].active_id == 2, "focus and active workspace");
    check(state.by_monitor["LVDS1"].workspaces.size() == 1 && state.by_monitor["LVDS1"].workspaces[0].occupied, "an occupied workspace is marked");
    check(state.clients.size() == 4 && state.clients[0].address == "5" && state.clients[0].at[0] == 100 && state.clients[0].at[1] == 70, "client positions are global");
    check(state.clients[1].focus_history_id == 0 && state.clients[0].focus_history_id != 0, "the focused window has history id 0");
    check(state.clients[3].floating && state.clients[3].at[0] == 200 && state.clients[3].at[1] == 190, "a floating window keeps its global position");

    std::vector<DockEntry> dock = dock_entries_for_monitor(state, "LVDS1");
    check(dock.size() == 4 && dock[0].window_class == "Firefox" && dock[1].window_class == "vlc" && dock[2].window_class == "mpv" && dock[3].window_class == "foot", "dock lists the workspace windows left to right");
    check(!dock[0].focused && !dock[1].focused && !dock[2].focused && dock[3].focused, "dock marks the focused window");
    check(dock_entries_for_monitor(state, "HDMI1").empty(), "dock is empty on an output without an active workspace");

    std::string encoded = i3_encode(4, "{}");
    check(encoded.size() == 14 + 2 && encoded.starts_with("i3-ipc"), "message framing");
    std::vector<I3Message> messages;
    std::string two = encoded + i3_encode(0x80000003u, "{\"change\":\"focus\"}");
    check(i3_decode(two.substr(0, 10), messages) == 0 && messages.empty(), "a partial header is kept");
    check(i3_decode(encoded.substr(0, 15), messages) == 0 && messages.empty(), "a partial body is kept");
    std::size_t consumed = i3_decode(two, messages);
    check(consumed == two.size() && messages.size() == 2, "two messages decode");
    check(messages.size() == 2 && messages[0].type == 4 && !messages[0].is_event() && messages[0].payload == "{}", "a reply message");
    check(messages.size() == 2 && messages[1].is_event() && (messages[1].type & 0x7fffffffu) == 3 && messages[1].payload.find("focus") != std::string::npos, "an event message");
    std::vector<I3Message> bad;
    check(i3_decode("not an i3 message at all", bad) == 24 && bad.empty(), "garbage is discarded");

    check(i3_socket_path("/fallback") == (std::getenv("I3SOCK") != nullptr && *std::getenv("I3SOCK") != '\0' ? std::string(std::getenv("I3SOCK")) : std::string("/fallback")), "the environment wins over the fallback");

    auto sway = parse_json(R"({"type":"root","nodes":[{"type":"output","name":"eDP-1","rect":{"x":0,"y":0,"width":1920,"height":1200},"nodes":[
        {"type":"workspace","num":1,"name":"1","rect":{"x":0,"y":0,"width":1920,"height":1200},"nodes":[
          {"id":11,"type":"con","window":null,"app_id":"foot","name":"shell","percent":0.5,"rect":{"x":0,"y":0,"width":960,"height":1200}},
          {"id":12,"type":"con","window":77,"app_id":null,"name":"legacy","percent":0.5,"rect":{"x":960,"y":0,"width":960,"height":1200},"window_properties":{"class":"Steam"}}]}]}]})");
    check(sway.has_value(), "a sway tree parses");
    if (sway) {
        I3Tree native = i3_parse_tree(*sway);
        check(native.windows.size() == 2 && native.windows[0].window_class == "foot" && native.windows[1].window_class == "Steam", "native Wayland windows are named by their app id");
    }
    setenv("I3SOCK", "", 1);
    setenv("SWAYSOCK", "/run/sway.sock", 1);
    check(i3_socket_path("") == "/run/sway.sock", "the Sway socket is used when I3SOCK is empty");
    setenv("I3SOCK", "/run/i3.sock", 1);
    check(i3_socket_path("") == "/run/i3.sock", "I3SOCK wins over SWAYSOCK");
    unsetenv("I3SOCK");
    unsetenv("SWAYSOCK");
}
