#include <algorithm>
#include <cmath>

#include "config/bar_layout.h"

#include "render/icons.h"

#include "modules/bar/model/model.h"
#include "modules/bar/model/zones.h"
#include "modules/bar/paint/chips.h"

namespace astralia::bar {

namespace {

namespace cfg = bar_layout;

constexpr ui::TextStyle icon_style{ui::FontFamily::icon, static_cast<int>(cfg::icon_px)};
constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::text_px};
constexpr ui::TextStyle name_style{ui::FontFamily::text, cfg::active_window_name_px};
constexpr ui::TextStyle title_style{ui::FontFamily::text, cfg::active_window_title_px};

struct Placed {
    Slot slot;
    float w;
};

} // namespace

float Model::item_width(const ItemView &view, float pad) const {
    float collapsed = view.icon_w + 2.0f * pad;
    float text = view.detail_w > 0.0f ? view.detail_w : view.label_w;
    if (view.label.empty() || text <= 0.0f) {
        return collapsed;
    }
    if (view.detail_w > 0.0f) {
        text = std::min(text, cfg::active_window_cap - collapsed - cfg::label_gap);
    }
    return collapsed + (text + cfg::label_gap) * view.expand;
}

void Model::layout(ui::Canvas &canvas, float width) {
    const StyleSpec &style = *style_;
    float height = cfg::height;
    float pad = height * style.pad_ratio;
    bool rail = style.has_rail();
    bool dividers_on = rail || style.continuous;
    float island_pad = rail ? cfg::island_pad : style.continuous ? height * style.pad_ratio : 0.0f;
    layout_ = {};
    layout_.width = width;
    layout_.height = height;

    for (size_t i = 0; i < item_count; ++i) {
        ItemView &view = items_[i];
        view.box = {};
        view.grouped = false;
        view.zone = PanelAnchor::right;
        view.detail_w = 0.0f;
        if (!view.visible) {
            continue;
        }
        if (measured_glyph_[i] != view.glyph) {
            measured_glyph_[i] = view.glyph;
            view.icon_w = canvas.measure(view.glyph, icon_style).w;
        }
        if (measured_label_[i] != view.label) {
            measured_label_[i] = view.label;
            view.label_w = view.label.empty() ? 0.0f : canvas.measure(view.label, text_style).w;
        }
        if (!view.detail.empty()) {
            view.detail_w = std::max(canvas.measure(view.detail, title_style).w,
                                     canvas.measure(view.label, name_style).w);
        }
    }
    for (DockSlot &slot : dock_) {
        if (auto m = dock_motion_.find(slot.entry.address); m != dock_motion_.end()) {
            slot.x = m->second.x;
            slot.appear = m->second.appear;
        }
    }

    float overview_w = canvas.measure(icon::overview, icon_style).w;
    float ws_row = 0.0f;
    for (size_t i = 0; i < workspaces_.size(); ++i) {
        ws_row +=
            (i > 0 ? style.workspace_gap : 0.0f) +
            style.workspace_size * (1.0f + (style.workspace_scale - 1.0f) * workspaces_[i].grow);
    }
    auto cell_extent = [&](const ItemView &view) {
        return icon_cell_extent(view.icon_w, view.label.empty() ? 0.0f : view.label_w, view.expand);
    };
    auto members = [&] {
        std::vector<Item> list;
        for (Item id : status_members) {
            if (item(id).visible) {
                list.push_back(id);
            }
        }
        return list;
    };

    auto slot_width = [&](const Slot &slot) {
        switch (slot.kind) {
        case SlotKind::workspaces:
            if (workspaces_.empty()) {
                return 0.0f;
            }
            return style.workspace_badges
                       ? ws_row + cfg::capsule_gap + overview_w + 2.0f * pad
                       : ws_row + 2.0f * pad + cfg::workspace_overview_gap + overview_w;
        case SlotKind::status: {
            std::vector<Item> list = members();
            float w = 0.0f;
            for (Item id : list) {
                w += cell_extent(item(id));
            }
            return list.empty()
                       ? 0.0f
                       : w + style.icon_gap * static_cast<float>(list.size() - 1) + 2.0f * pad;
        }
        case SlotKind::dock:
            return dock_.empty() ? 0.0f : dock_w_ + 2.0f * pad;
        case SlotKind::item:
            break;
        }
        const ItemView &view = item(slot.item);
        return view.visible ? item_width(view, pad) : 0.0f;
    };

    auto place = [&](const Slot &slot, float x, float w, PanelAnchor where) {
        switch (slot.kind) {
        case SlotKind::workspaces: {
            layout_.workspaces = {x, 0.0f, w, height};
            bool badges = style.workspace_badges;
            float wx = badges ? x : x + pad;
            float wy = badges ? 0.0f : (height - style.workspace_size) / 2.0f;
            for (BarWorkspace &ws : workspaces_) {
                float bw = style.workspace_size * (1.0f + (style.workspace_scale - 1.0f) * ws.grow);
                ws.box = {wx, wy, bw, badges ? height : style.workspace_size};
                wx += bw + style.workspace_gap;
            }
            float end = wx - style.workspace_gap;
            layout_.overview =
                badges ? ui::Box{end + cfg::capsule_gap, 0.0f, overview_w + 2.0f * pad, height}
                       : ui::Box{end + cfg::workspace_overview_gap / 2.0f, 0.0f,
                                 overview_w + cfg::workspace_overview_gap, height};
            break;
        }
        case SlotKind::status: {
            layout_.status = {x, 0.0f, w, height};
            std::vector<Item> list = members();
            float at = x + pad;
            for (size_t i = 0; i < list.size(); ++i) {
                ItemView &view = mut(list[i]);
                float left = i == 0 ? x : at - style.icon_gap / 2.0f;
                at += cell_extent(view) + style.icon_gap;
                float right = i + 1 == list.size() ? x + w : at - style.icon_gap / 2.0f;
                view.box = {left, 0.0f, right - left, height};
                view.grouped = true;
                view.zone = where;
            }
            break;
        }
        case SlotKind::dock:
            layout_.dock = {x, 0.0f, w, height};
            break;
        case SlotKind::item:
            mut(slot.item).box = {x, 0.0f, w, height};
            mut(slot.item).zone = where;
            break;
        }
    };

    struct Run {
        float start = 0.0f;
        float end = 0.0f;
        bool any = false;
    };
    auto measure = [&](const std::vector<Slot> &zone) {
        std::vector<Placed> placed;
        for (const Slot &slot : zone) {
            if (float w = slot_width(slot); w > 0.0f) {
                placed.push_back({slot, w});
            }
        }
        return placed;
    };
    auto total = [&](const std::vector<Placed> &placed) {
        float sum = 0.0f;
        for (const Placed &p : placed) {
            sum += p.w;
        }
        return placed.empty() ? 0.0f
                              : sum + cfg::capsule_gap * static_cast<float>(placed.size() - 1);
    };
    auto run = [&](const std::vector<Placed> &placed, float start, PanelAnchor where) {
        float x = start;
        for (size_t i = 0; i < placed.size(); ++i) {
            if (i > 0) {
                x += cfg::capsule_gap;
                if (dividers_on) {
                    layout_.dividers.push_back(x - cfg::capsule_gap / 2.0f);
                }
            }
            place(placed[i].slot, x, placed[i].w, where);
            x += placed[i].w;
        }
        return Run{start, x, !placed.empty()};
    };

    const Zones &zones = *style.zones;
    std::vector<Placed> left = measure(zones.left);
    std::vector<Placed> center = measure(zones.center);
    std::vector<Placed> right = measure(zones.right);
    float center_w = total(center);
    float right_w = total(right);
    Run l = run(left, island_pad, PanelAnchor::left);
    Run c = run(center, (width - center_w) / 2.0f, PanelAnchor::center);
    Run r = run(right, width - island_pad - right_w, PanelAnchor::right);
    if (rail) {
        if (l.any) {
            layout_.left_end = std::round(l.end + island_pad);
        }
        if (c.any) {
            layout_.center =
                IslandSpan{std::round(c.start - island_pad), std::round(c.end + island_pad)};
        }
        if (r.any) {
            layout_.right_start = std::round(r.start - island_pad);
        }
    }
}

} // namespace astralia::bar
