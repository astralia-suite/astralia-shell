#include "service/wayland/toplevel_service.h"

namespace {

void handle_title(void *data, zwlr_foreign_toplevel_handle_v1 *, const char *title) {
    static_cast<ToplevelHandle *>(data)->title = title != nullptr ? title : "";
}

void handle_app_id(void *data, zwlr_foreign_toplevel_handle_v1 *, const char *app_id) {
    static_cast<ToplevelHandle *>(data)->app_id = app_id != nullptr ? app_id : "";
}

void handle_output_enter(void *, zwlr_foreign_toplevel_handle_v1 *, wl_output *) {}

void handle_output_leave(void *, zwlr_foreign_toplevel_handle_v1 *, wl_output *) {}

void handle_state(void *, zwlr_foreign_toplevel_handle_v1 *, wl_array *) {}

void handle_done(void *, zwlr_foreign_toplevel_handle_v1 *) {}

void handle_closed(void *data, zwlr_foreign_toplevel_handle_v1 *handle) {
    auto *entry = static_cast<ToplevelHandle *>(data);
    ToplevelList *list = entry->list;
    zwlr_foreign_toplevel_handle_v1_destroy(handle);
    std::erase_if(list->handles, [&](const std::unique_ptr<ToplevelHandle> &candidate) {
        return candidate.get() == entry;
    });
}

void handle_parent(void *, zwlr_foreign_toplevel_handle_v1 *, zwlr_foreign_toplevel_handle_v1 *) {}

constexpr zwlr_foreign_toplevel_handle_v1_listener handle_listener{
    handle_title, handle_app_id, handle_output_enter, handle_output_leave,
    handle_state, handle_done,   handle_closed,       handle_parent,
};

void manager_toplevel(void *data, zwlr_foreign_toplevel_manager_v1 *,
                      zwlr_foreign_toplevel_handle_v1 *handle) {
    auto *list = static_cast<ToplevelList *>(data);
    auto entry = std::make_unique<ToplevelHandle>();
    entry->list = list;
    entry->handle = handle;
    zwlr_foreign_toplevel_handle_v1_add_listener(handle, &handle_listener, entry.get());
    list->handles.push_back(std::move(entry));
}

void manager_finished(void *data, zwlr_foreign_toplevel_manager_v1 *manager) {
    auto *list = static_cast<ToplevelList *>(data);
    zwlr_foreign_toplevel_manager_v1_destroy(manager);
    if (list->manager == manager) {
        list->manager = nullptr;
    }
}

constexpr zwlr_foreign_toplevel_manager_v1_listener manager_listener{manager_toplevel,
                                                                     manager_finished};

} // namespace

void toplevel_list_bind(ToplevelList &list, zwlr_foreign_toplevel_manager_v1 *manager) {
    list.manager = manager;
    zwlr_foreign_toplevel_manager_v1_add_listener(manager, &manager_listener, &list);
}

std::unordered_map<std::string, zwlr_foreign_toplevel_handle_v1 *>
toplevel_handles_for(const ToplevelList &list,
                     const std::vector<astralia::CompositorClient> &clients) {
    std::vector<astralia::ToplevelIdentity> identities;
    identities.reserve(list.handles.size());
    for (const std::unique_ptr<ToplevelHandle> &entry : list.handles) {
        identities.push_back({entry->app_id, entry->title});
    }
    std::unordered_map<std::string, zwlr_foreign_toplevel_handle_v1 *> handles;
    for (const auto &[address, index] : astralia::pair_toplevels(clients, identities)) {
        handles.emplace(address, list.handles[index]->handle);
    }
    return handles;
}
