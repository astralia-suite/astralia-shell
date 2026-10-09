#include <filesystem>
#include <memory>

#include "check.h"

#include "core/poll_reactor.h"

#include "ui/module_base.h"

namespace {

using namespace astralia;

class FakeHost : public ui::Host {
  public:
    explicit FakeHost(Reactor &reactor) : reactor_(reactor) {}

    Reactor &reactor() override { return reactor_; }
    std::vector<ui::OutputInfo> outputs() const override { return {{"A-1", {0, 0, 1920, 1080}, 1.0f}}; }
    std::unique_ptr<ui::Surface> create_surface(const ui::SurfaceSpec &) override { return nullptr; }

  private:
    Reactor &reactor_;
};

class FakeModule : public ui::ModuleBase {
  public:
    using ModuleBase::ModuleBase;

    std::string_view name() const override { return "fake"; }
    bool is_open() const override { return open; }

    std::vector<IpcHandler> ipc_handlers() override {
        return {
            {"fake-open", [this] { open = true; return std::string("opened\n"); }, "open the fake"},
            {"fake-close", [this] { open = false; return std::string("closed\n"); }, "close the fake"},
        };
    }
    void on_config(const Config &config) override { seen_bar = config.bar_style; }
    bool on_key(const input::KeyEvent &event) override {
        last_key = event.kind;
        return event.kind == input::KeyKind::Escape;
    }
    bool on_scroll(const input::ScrollEvent &event) override {
        scrolled += event.dy;
        return true;
    }

    using ModuleBase::config;
    using ModuleBase::host;

    bool open = false;
    BarStyle seen_bar = BarStyle::islands;
    input::KeyKind last_key = input::KeyKind::Text;
    double scrolled = 0;
};

} // namespace

void check_module_base() {
    auto reactor = PollReactor::create();
    test::check(reactor.has_value(), "reactor created");
    if (!reactor) {
        return;
    }
    FakeHost host(*reactor);
    Config config;
    FakeModule module({host, *reactor, config});

    test::check(module.name() == "fake" && !module.is_open(), "starts closed");
    test::check(&module.host() == &host && &module.config() == &config, "context reaches the module");
    test::check(host.outputs().size() == 1 && host.outputs().front().geometry.width == 1920, "host reports outputs");

    std::string path = std::filesystem::temp_directory_path() / "astralia-module-base-test.sock";
    auto server = IpcServer::create(*reactor, path);
    test::check(server.has_value(), "ipc server created");
    if (server) {
        ui::register_ipc(**server, module);
        test::check((*server)->dispatch("fake-open") == "opened\n" && module.is_open(), "registered verb runs");
        test::check((*server)->dispatch("fake-close") == "closed\n" && !module.is_open(), "second verb runs");
    }
    std::filesystem::remove(path);

    input::KeyEvent escape;
    escape.kind = input::KeyKind::Escape;
    test::check(module.on_key(escape) && module.last_key == input::KeyKind::Escape, "key reaches the module");
    test::check(module.on_scroll({2.0}) && module.on_scroll({-0.5}) && module.scrolled == 1.5, "scroll accumulates");
    test::check(!module.on_pointer({}), "unhandled pointer defaults to false");

    Config other;
    other.bar_style = BarStyle::continuous;
    module.apply_config(other);
    test::check(module.seen_bar == BarStyle::continuous && &module.config() == &other, "config is swapped and announced");
}
