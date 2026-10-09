#pragma once

#include <chrono>
#include <string_view>
#include <vector>

#include "core/input.h"
#include "core/ipc.h"
#include "core/reactor.h"

#include "service/settings_service.h"

#include "ui/host.h"

namespace astralia::ui {

struct ModuleContext {
    Host &host;
    Reactor &reactor;
    const Config &config;
};

class ModuleBase {
  public:
    explicit ModuleBase(ModuleContext context)
        : host_(context.host), reactor_(context.reactor), config_(&context.config) {}
    virtual ~ModuleBase() = default;

    virtual std::string_view name() const = 0;
    virtual bool is_open() const = 0;

    virtual std::vector<IpcHandler> ipc_handlers() { return {}; }
    virtual void on_config(const Config &) {}
    virtual bool on_key(const input::KeyEvent &) { return false; }
    virtual bool on_pointer(const input::PointerEvent &) { return false; }
    virtual bool on_scroll(const input::ScrollEvent &) { return false; }
    virtual bool on_tick(std::chrono::steady_clock::time_point) { return false; }

    void apply_config(const Config &config);

  protected:
    Host &host() { return host_; }
    Reactor &reactor() { return reactor_; }
    const Config &config() const { return *config_; }

  private:
    Host &host_;
    Reactor &reactor_;
    const Config *config_;
};

void register_ipc(IpcServer &server, ModuleBase &module);

} // namespace astralia::ui
