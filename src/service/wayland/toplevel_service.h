#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "service/compositor_service.h"

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

struct ToplevelList;

struct ToplevelHandle {
    ToplevelList *list = nullptr;
    zwlr_foreign_toplevel_handle_v1 *handle = nullptr;
    std::string app_id;
    std::string title;
};

struct ToplevelList {
    zwlr_foreign_toplevel_manager_v1 *manager = nullptr;
    std::vector<std::unique_ptr<ToplevelHandle>> handles;
};

void toplevel_list_bind(ToplevelList &list, zwlr_foreign_toplevel_manager_v1 *manager);

std::unordered_map<std::string, zwlr_foreign_toplevel_handle_v1 *>
toplevel_handles_for(const ToplevelList &list,
                     const std::vector<astralia::CompositorClient> &clients);
