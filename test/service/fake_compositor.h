#pragma once

#include <string>
#include <vector>

#include "service/compositor_service.h"

namespace test {

class FakeCompositor final : public astralia::Compositor {
  public:
    const char *name() const override { return "fake"; }
    const astralia::CompositorState &state() const override { return state_; }
    void refresh() override { calls.push_back("refresh"); }
    bool refresh_clients() override { return false; }
    void watch_client_order(bool) override {}
    void focus_workspace(int id, bool global) override { calls.push_back("focus " + std::to_string(id) + (global ? " global" : "")); }
    void move_window(const std::string &address, int id, bool global) override { calls.push_back("move " + address + " " + std::to_string(id) + (global ? " global" : "")); }
    void close_window(const std::string &address) override { calls.push_back("close " + address); }
    void close_windows(astralia::CloseScope scope, int id) override {
        const char *name = scope == astralia::CloseScope::workspace ? "workspace" : scope == astralia::CloseScope::monitor ? "monitor"
                                                                                                                           : "all";
        calls.push_back(std::string("close-windows ") + name + " " + std::to_string(id));
    }
    void move_workspace_in(int id, bool global) override { calls.push_back("move-in " + std::to_string(id) + (global ? " global" : "")); }
    void swap_workspace(int id, bool global) override { calls.push_back("swap " + std::to_string(id) + (global ? " global" : "")); }

    astralia::CompositorState state_;
    std::vector<std::string> calls;
};

inline astralia::CompositorMonitor monitor(int id, std::string name, double x, double y, double width, double height) {
    astralia::CompositorMonitor m;
    m.id = id;
    m.name = std::move(name);
    m.x = x;
    m.y = y;
    m.width = width;
    m.height = height;
    return m;
}

} // namespace test
