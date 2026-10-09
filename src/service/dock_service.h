#pragma once

#include <string>
#include <vector>

#include "service/compositor_service.h"

namespace astralia {

struct DockEntry {
    std::string address;
    std::string window_class;
    bool focused = false;

    bool operator==(const DockEntry &) const = default;
};

std::vector<DockEntry> dock_entries_for_monitor(const CompositorState &compositor, const std::string &monitor_name);

} // namespace astralia
