#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "config/bar_layout.h"

#include "modules/bar/model/model.h"

#include "service/dock_service.h"

namespace astralia::bar {

namespace cfg = bar_layout;

void Model::refresh_workspaces() {
    workspaces_.clear();
    if (!sources_.compositor) {
        return;
    }
    const CompositorState &state = sources_.compositor();
    std::vector<Workspace> list;
    int active_id = -1;
    auto it = state.by_monitor.find(sources_.output);
    if (sources_.workspace_slots > 0) {
        if (it == state.by_monitor.end() || it->second.active_id < 1) {
            it = state.by_monitor.find(state.focused_monitor);
        }
        active_id = it != state.by_monitor.end() ? it->second.active_id : -1;
        for (int id = 1; id <= sources_.workspace_slots; ++id) {
            bool occupied = false;
            for (const auto &[monitor, entry] : state.by_monitor) {
                occupied |= std::ranges::any_of(entry.workspaces, [&](const Workspace &ws) {
                    return ws.id == id && ws.occupied;
                });
            }
            list.push_back({id, std::to_string(id), occupied});
        }
    } else if (it != state.by_monitor.end()) {
        list = it->second.workspaces;
        active_id = it->second.active_id;
    } else {
        return;
    }
    std::unordered_set<int> live;
    for (const Workspace &ws : list) {
        bool active = ws.id == active_id;
        live.insert(ws.id);
        auto seen = was_active_.find(ws.id);
        if (seen == was_active_.end()) {
            grow_[ws.id] = active ? 1.0f : 0.0f;
            was_active_[ws.id] = active;
        } else if (seen->second != active) {
            seen->second = active;
            int id = ws.id;
            animations_.animate(
                grow_[id], active ? 1.0f : 0.0f, cfg::workspace_anim_ms, astralia::Easing::Linear,
                [this, id](float v) { grow_[id] = v; }, {},
                cfg::owner_workspace + static_cast<uint64_t>(id));
        }
        workspaces_.push_back({ws.id, active, ws.occupied, grow_[ws.id], {}});
    }
    std::erase_if(grow_, [&](const auto &kv) { return !live.contains(kv.first); });
    std::erase_if(was_active_, [&](const auto &kv) { return !live.contains(kv.first); });
}

void Model::refresh_dock() {
    std::vector<DockEntry> entries;
    if (sources_.compositor) {
        entries = dock_entries_for_monitor(sources_.compositor(), sources_.output);
    }
    std::unordered_set<std::string> live;
    dock_.clear();
    for (size_t i = 0; i < entries.size(); ++i) {
        const DockEntry &entry = entries[i];
        live.insert(entry.address);
        float target = static_cast<float>(i) * (cfg::dock_icon + cfg::dock_spacing);
        auto found = dock_motion_.find(entry.address);
        if (found == dock_motion_.end()) {
            DockMotion fresh;
            fresh.x = fresh.target = target;
            fresh.owner = cfg::owner_dock + 2 * next_dock_owner_++;
            fresh.appear = dock_seen_ ? 0.0f : 1.0f;
            found = dock_motion_.emplace(entry.address, fresh).first;
            if (fresh.appear < 1.0f) {
                std::string address = entry.address;
                animations_.animate(
                    0.0f, 1.0f, cfg::dock_reorder_ms, astralia::Easing::EaseOutQuad,
                    [this, address](float v) {
                        if (auto m = dock_motion_.find(address); m != dock_motion_.end()) {
                            m->second.appear = v;
                        }
                    },
                    {}, fresh.owner + 1);
            }
        }
        DockMotion &motion = found->second;
        if (std::fabs(motion.target - target) > 0.5f) {
            motion.target = target;
            std::string address = entry.address;
            animations_.animate(
                motion.x, target, cfg::dock_reorder_ms, astralia::Easing::EaseOutQuad,
                [this, address](float v) {
                    if (auto m = dock_motion_.find(address); m != dock_motion_.end()) {
                        m->second.x = v;
                    }
                },
                {}, motion.owner);
        }
        dock_.push_back({entry, motion.x, motion.appear});
    }
    for (auto it = dock_motion_.begin(); it != dock_motion_.end();) {
        if (live.contains(it->first)) {
            ++it;
            continue;
        }
        animations_.cancel_owner(it->second.owner);
        animations_.cancel_owner(it->second.owner + 1);
        it = dock_motion_.erase(it);
    }

    float width_target = entries.empty()
                             ? 0.0f
                             : static_cast<float>(entries.size()) * cfg::dock_icon +
                                   static_cast<float>(entries.size() - 1) * cfg::dock_spacing;
    if (entries.empty() || !dock_seen_ || dock_w_ <= 0.0f) {
        animations_.cancel_owner(cfg::owner_dock_width);
        dock_w_ = width_target;
        dock_w_target_ = width_target;
    } else if (std::fabs(dock_w_target_ - width_target) > 0.5f) {
        dock_w_target_ = width_target;
        animations_.animate(
            dock_w_, width_target, cfg::dock_reorder_ms, astralia::Easing::EaseOutQuad,
            [this](float v) { dock_w_ = v; }, {}, cfg::owner_dock_width);
    }
    dock_seen_ = dock_seen_ || !entries.empty();
}

} // namespace astralia::bar
