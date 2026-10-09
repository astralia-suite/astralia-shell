#include "render/module_base.h"

namespace astralia::ui {

void ModuleBase::apply_config(const Config &config) {
    config_ = &config;
    on_config(config);
}

void register_ipc(IpcServer &server, ModuleBase &module) {
    for (IpcHandler &handler : module.ipc_handlers()) {
        server.add(std::move(handler));
    }
}

} // namespace astralia::ui
