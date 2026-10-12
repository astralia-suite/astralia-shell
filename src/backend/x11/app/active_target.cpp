#include "app/active_target.h"

namespace astralia {

const Output &active_target_output(Services &services) {
    std::string wanted = std::move(services.overlay_hint);
    services.overlay_hint.clear();
    if (wanted.empty()) {
        wanted = services.compositor->state().focused_monitor;
    }
    if (!wanted.empty()) {
        for (const Output &output : services.outputs.outputs()) {
            if (output.name == wanted) {
                return output;
            }
        }
    }
    return services.outputs.at_pointer();
}

} // namespace astralia
