#include <algorithm>

#include "service/dock_service.h"

namespace astralia {

std::vector<DockEntry> dock_entries_for_monitor(const CompositorState &compositor, const std::string &monitor_name) {
    auto it = compositor.by_monitor.find(monitor_name);
    if (it == compositor.by_monitor.end() || it->second.active_id < 0) {
        return {};
    }
    int active_id = it->second.active_id;
    std::vector<const CompositorClient *> matched;
    for (const CompositorClient &client : compositor.clients) {
        if (client.workspace_id == active_id) {
            matched.push_back(&client);
        }
    }
    std::ranges::stable_sort(matched, [](const CompositorClient *a, const CompositorClient *b) { return a->at[0] < b->at[0]; });
    std::vector<DockEntry> entries;
    entries.reserve(matched.size());
    for (const CompositorClient *client : matched) {
        entries.push_back({client->address, client->window_class, client->focus_history_id == 0});
    }
    return entries;
}

} // namespace astralia
