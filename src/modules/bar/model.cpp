#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "config/bar_layout.h"
#include "config/icons.h"

#include "modules/bar/model.h"

#include "service/icon_service.h"

#include "ui/glyphs.h"

namespace astralia {

namespace {

namespace cfg = bar_layout;

constexpr ui::TextStyle icon_style{ui::FontFamily::icon, static_cast<int>(cfg::icon_px)};
constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::text_px};

bool inside(const ui::Box &box, double x, double y) {
    return box.w > 0.0f && x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h;
}

} // namespace

const char *bar_wifi_glyph(int strength) {
    if (strength > 75) {
        return icon::wifi;
    }
    if (strength > 50) {
        return icon::wifi2;
    }
    if (strength > 25) {
        return icon::wifi1;
    }
    return icon::wifi0;
}

const char *bar_bluetooth_glyph(const BluetoothStatus &status) {
    if (!status.powered) {
        return icon::bluetooth_off;
    }
    return status.connected ? icon::bluetooth_connected : icon::bluetooth_on;
}

std::string bar_bluetooth_label(const BluetoothStatus &status) {
    if (!status.present) {
        return "";
    }
    if (!status.powered) {
        return "Disconnected";
    }
    return status.connected && !status.device.empty() ? status.device : "Idle";
}

std::chrono::milliseconds ms_until_next_second(std::chrono::system_clock::time_point now) {
    auto next = std::chrono::floor<std::chrono::seconds>(now) + std::chrono::seconds(1);
    return std::chrono::ceil<std::chrono::milliseconds>(next - now);
}

const char *bar_battery_glyph(const BatteryStatus &status) {
    if (status.full) {
        return icon::plugged_in;
    }
    if (status.charging) {
        return icon::battery_charging;
    }
    if (status.percent <= 25) {
        return icon::battery1;
    }
    if (status.percent <= 50) {
        return icon::battery2;
    }
    if (status.percent <= 75) {
        return icon::battery3;
    }
    return icon::battery4;
}

std::string bar_battery_label(const BatteryStatus &status) {
    return status.full ? "Plugged in" : std::to_string(status.percent) + "%";
}

BarModel::BarModel(BarSources sources, Clock::time_point now) : sources_(std::move(sources)), style_(&bar_style_spec(BarStyle::continuous, false)), started_(now) {}

void BarModel::refresh(Clock::time_point now) {
    auto set = [&](BarItem id, const char *glyph, std::string label, bool visible) {
        BarItemView &view = mut(id);
        view.glyph = visible ? glyph : nullptr;
        view.label = visible ? std::move(label) : std::string();
        view.visible = visible && glyph != nullptr;
        view.border.reset();
        view.alert = false;
    };
    set(BarItem::logout, icon::power, "Logout", true);
    set(BarItem::tray, icon::tray, "Tray", true);
    set(BarItem::resource, icon::cpu, "Resource", sources_.resource);
    set(BarItem::media, icon::music_note, "Media", true);
    set(BarItem::clock, icon::clock, clock_label_, true);

    if (sources_.network != nullptr) {
        const NetworkService &net = *sources_.network;
        bool portal = net.connectivity() == "portal" || net.status().portal;
        const char *glyph = icon::wifi_off;
        std::string label;
        if (portal) {
            glyph = icon::lock;
            label = "Sign in";
        } else if (net.ethernet_connected()) {
            glyph = icon::router;
            label = "Ethernet";
        } else if (net.status().kind == NetworkKind::wifi) {
            glyph = bar_wifi_glyph(net.status().strength);
            label = net.status().ssid;
        } else if (net.ethernet_available() && !net.wifi_available()) {
            glyph = icon::router;
        }
        set(BarItem::network, glyph, label.empty() ? "Wi-Fi" : label, true);
    } else {
        set(BarItem::network, nullptr, "", false);
    }

    if (sources_.bluetooth != nullptr && sources_.bluetooth->status().present) {
        const BluetoothStatus &status = sources_.bluetooth->status();
        const char *glyph = bar_bluetooth_glyph(status);
        std::string label = bar_bluetooth_label(status);
        set(BarItem::bluetooth, glyph, label, true);
    } else {
        set(BarItem::bluetooth, nullptr, "", false);
    }

    if (sources_.audio != nullptr && sources_.audio->sink().present) {
        AudioLevel level = sources_.audio->sink();
        set(BarItem::volume, icon::volume_threshold(level.muted, level.percent), level.muted ? "muted" : std::to_string(level.percent) + "%", true);
        if (!peek_ready_ && now - started_ >= cfg::peek_ready_delay) {
            peek_ready_ = true;
        }
        if (peek_ready_) {
            bool changed = peek_level_ < 0 || peek_level_ != level.percent || peek_muted_ != level.muted;
            peek_level_ = level.percent;
            peek_muted_ = level.muted;
            if (changed) {
                peek_active_ = true;
                peek_deadline_ = now + cfg::peek_duration;
            }
        } else {
            peek_level_ = level.percent;
            peek_muted_ = level.muted;
        }
    } else {
        set(BarItem::volume, nullptr, "", false);
    }

    if (sources_.brightness != nullptr && sources_.brightness->available()) {
        int percent = sources_.brightness->percent();
        set(BarItem::brightness, icon::brightness_threshold(percent), std::to_string(percent) + "%", true);
    } else {
        set(BarItem::brightness, nullptr, "", false);
    }

    if (sources_.battery != nullptr && sources_.battery->status().present) {
        const BatteryStatus &status = sources_.battery->status();
        set(BarItem::battery, bar_battery_glyph(status), bar_battery_label(status), true);
        BarItemView &view = mut(BarItem::battery);
        view.border = status.full ? palette::text : status.charging    ? palette::accent_alt
                                                : status.percent <= 25 ? palette::critical
                                                                       : palette::accent;
        view.alert = !status.charging && !status.full && status.percent <= cfg::battery_critical_percent;
    } else {
        set(BarItem::battery, nullptr, "", false);
    }

    refresh_workspaces();
    refresh_dock();
    apply_hover(now);
}

void BarModel::refresh_workspaces() {
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
                occupied |= std::ranges::any_of(entry.workspaces, [&](const Workspace &ws) { return ws.id == id && ws.occupied; });
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
            animations_.animate(grow_[id], active ? 1.0f : 0.0f, cfg::workspace_anim_ms, astralia::Easing::Linear, [this, id](float v) { grow_[id] = v; }, {}, cfg::owner_workspace + static_cast<uint64_t>(id));
        }
        workspaces_.push_back({ws.id, active, ws.occupied, grow_[ws.id], {}});
    }
    std::erase_if(grow_, [&](const auto &kv) { return !live.contains(kv.first); });
    std::erase_if(was_active_, [&](const auto &kv) { return !live.contains(kv.first); });
}

void BarModel::refresh_dock() {
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
        auto slot = dock_x_.find(entry.address);
        if (slot == dock_x_.end()) {
            dock_x_[entry.address] = target;
        } else if (std::fabs(slot->second - target) > 0.5f) {
            std::string address = entry.address;
            animations_.animate(slot->second, target, cfg::dock_reorder_ms, astralia::Easing::EaseOutQuad, [this, address](float v) { dock_x_[address] = v; }, {}, cfg::owner_dock + i);
        }
        dock_.push_back({entry, dock_x_[entry.address]});
    }
    std::erase_if(dock_x_, [&](const auto &kv) { return !live.contains(kv.first); });
}

bool BarModel::update_expand(BarItem id, bool target, bool instant) {
    BarItemView &view = mut(id);
    if (view.hovered == target) {
        return false;
    }
    view.hovered = target;
    size_t index = static_cast<size_t>(id);
    animations_.animate(view.expand, target ? 1.0f : 0.0f, instant ? 0.0f : cfg::expand_ms, astralia::Easing::EaseOutCubic, [this, index](float v) { items_[index].expand = v; }, {}, cfg::owner_expand + index);
    return true;
}

bool BarModel::apply_hover(Clock::time_point now) {
    bool changed = false;
    std::optional<BarItem> target;
    if (open_) {
        target = open_;
    } else if (linger_ && now < linger_until_) {
        target = linger_;
    } else {
        target = hovered_;
    }
    if (!target && peek_active_) {
        target = BarItem::volume;
    }
    for (size_t i = 0; i < bar_item_count; ++i) {
        BarItem id = static_cast<BarItem>(i);
        const BarItemView &view = items_[i];
        bool want = view.visible && target == id && !view.label.empty();
        changed |= update_expand(id, want, want && open_ == id);
    }
    return changed;
}

void BarModel::set_open(std::optional<BarItem> item, Clock::time_point now) {
    if (item == open_) {
        return;
    }
    if (!item && open_) {
        linger_ = open_;
        linger_until_ = now + cfg::close_linger;
    }
    open_ = item;
    apply_hover(now);
}

bool BarModel::hover(std::optional<double> x, std::optional<double> y, Clock::time_point now) {
    pointer_x_ = x;
    pointer_y_ = y;
    hovered_.reset();
    if (x && y) {
        for (size_t i = 0; i < bar_item_count; ++i) {
            const BarItemView &view = items_[i];
            if (view.visible && inside(view.box, *x, *y)) {
                hovered_ = static_cast<BarItem>(i);
                break;
            }
        }
    }
    return apply_hover(now);
}

bool BarModel::tick(Clock::time_point now) {
    if (peek_active_ && now >= peek_deadline_) {
        peek_active_ = false;
    }
    animations_.tick(now);
    return apply_hover(now);
}

std::chrono::milliseconds BarModel::until_idle(Clock::time_point now) const {
    std::chrono::milliseconds wait = std::chrono::hours(1);
    auto consider = [&](Clock::time_point at) {
        auto left = std::chrono::ceil<std::chrono::milliseconds>(at - now);
        wait = std::min(wait, std::max(left, std::chrono::milliseconds(1)));
    };
    if (peek_active_) {
        consider(peek_deadline_);
    }
    if (linger_ && now < linger_until_) {
        consider(linger_until_);
    }
    return wait;
}

float BarModel::item_width(const BarItemView &view, float pad) const {
    float collapsed = view.icon_w + 2.0f * pad;
    if (view.label.empty() || view.label_w <= 0.0f) {
        return collapsed;
    }
    float expanded = 2.0f * pad + view.icon_w + cfg::label_gap + view.label_w;
    return collapsed + (expanded - collapsed) * view.expand;
}

void BarModel::layout(ui::Canvas &canvas, float width) {
    const BarStyleSpec &style = *style_;
    float height = cfg::height;
    float pad = height * style.pad_ratio;
    bool rail = style.has_rail();
    bool dividers_on = rail || style.continuous;
    float island_pad = rail ? cfg::island_pad : style.continuous ? height * style.pad_ratio
                                                                 : 0.0f;
    layout_ = {};
    layout_.width = width;
    layout_.height = height;

    for (size_t i = 0; i < bar_item_count; ++i) {
        BarItemView &view = items_[i];
        view.box = {};
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
    }

    auto place = [&](BarItem id, float x) {
        BarItemView &view = mut(id);
        view.box = {x, 0.0f, item_width(view, pad), height};
        return view.box.w;
    };
    auto run = [&](std::initializer_list<BarItem> ids, float x, std::vector<float> &dividers) {
        bool first = true;
        for (BarItem id : ids) {
            if (!item(id).visible) {
                continue;
            }
            if (!first) {
                x += cfg::capsule_gap;
                if (dividers_on) {
                    dividers.push_back(x - cfg::capsule_gap / 2.0f);
                }
            }
            x += place(id, x);
            first = false;
        }
        return x;
    };

    std::vector<float> left_dividers;
    float x = island_pad;
    float item_from = x;
    auto track = [&] {
        if (dividers_on && x > item_from) {
            left_dividers.push_back(x - cfg::capsule_gap / 2.0f);
        }
        item_from = x;
    };
    x += place(BarItem::logout, x) + cfg::capsule_gap;
    track();

    float grow_w = 0.0f;
    for (size_t i = 0; i < workspaces_.size(); ++i) {
        const BarWorkspace &ws = workspaces_[i];
        grow_w += (i > 0 ? cfg::workspace_pill_spacing : 0.0f) + cfg::workspace_pill_height * (1.0f + (cfg::workspace_active_scale - 1.0f) * ws.grow);
    }
    if (grow_w > 0.0f) {
        float overview_w = canvas.measure(icon::overview, icon_style).w;
        float row_w = grow_w + 2.0f * pad + cfg::workspace_overview_gap + overview_w;
        layout_.workspaces = {x, 0.0f, row_w, height};
        float wx = x + pad;
        float wy = (height - cfg::workspace_pill_height) / 2.0f;
        for (BarWorkspace &ws : workspaces_) {
            float w = cfg::workspace_pill_height * (1.0f + (cfg::workspace_active_scale - 1.0f) * ws.grow);
            ws.box = {wx, wy, w, cfg::workspace_pill_height};
            wx += w + cfg::workspace_pill_spacing;
        }
        float icon_x = wx - cfg::workspace_pill_spacing + cfg::workspace_overview_gap;
        layout_.overview = {icon_x - cfg::workspace_overview_gap / 2.0f, 0.0f, overview_w + cfg::workspace_overview_gap, height};
        x += row_w + cfg::capsule_gap;
        track();
    }
    if (!dock_.empty()) {
        float row_w = static_cast<float>(dock_.size()) * cfg::dock_icon + static_cast<float>(dock_.size() - 1) * cfg::dock_spacing;
        layout_.dock = {x, 0.0f, row_w + 2.0f * pad, height};
        x += row_w + 2.0f * pad + cfg::capsule_gap;
        track();
    }
    if (rail && x > island_pad) {
        layout_.left_end = std::round(x - cfg::capsule_gap + island_pad);
    }
    if (dividers_on) {
        for (size_t i = 0; i + 1 < left_dividers.size(); ++i) {
            layout_.dividers.push_back(left_dividers[i]);
        }
    }

    float center_w = 0.0f;
    bool first = true;
    for (BarItem id : {BarItem::media, BarItem::clock}) {
        if (item(id).visible) {
            center_w += (first ? 0.0f : cfg::capsule_gap) + item_width(item(id), pad);
            first = false;
        }
    }
    if (center_w > 0.0f) {
        float center_x = (width - center_w) / 2.0f;
        run({BarItem::media, BarItem::clock}, center_x, layout_.dividers);
        if (rail) {
            layout_.center = IslandSpan{std::round(center_x - island_pad), std::round(center_x + center_w + island_pad)};
        }
    }

    constexpr BarItem right_items[] = {BarItem::tray, BarItem::resource, BarItem::network, BarItem::bluetooth, BarItem::volume, BarItem::brightness, BarItem::battery};
    float right_w = 0.0f;
    first = true;
    for (BarItem id : right_items) {
        if (item(id).visible) {
            right_w += (first ? 0.0f : cfg::capsule_gap) + item_width(item(id), pad);
            first = false;
        }
    }
    if (right_w > 0.0f) {
        float right_x = width - island_pad - right_w;
        run({BarItem::tray, BarItem::resource, BarItem::network, BarItem::bluetooth, BarItem::volume, BarItem::brightness, BarItem::battery}, right_x, layout_.dividers);
        if (rail) {
            layout_.right_start = std::round(right_x - island_pad);
        }
    }
}

BarFrame BarModel::frame() const {
    return bar_frame(*style_, layout_.left_end, layout_.center, layout_.right_start, layout_.width);
}

BarAction BarModel::press(double x, double y) const {
    for (size_t i = 0; i < bar_item_count; ++i) {
        const BarItemView &view = items_[i];
        if (view.visible && inside(view.box, x, y)) {
            return {BarActionKind::toggle, static_cast<BarItem>(i), 0};
        }
    }
    if (inside(layout_.overview, x, y)) {
        return {BarActionKind::overview};
    }
    for (const BarWorkspace &ws : workspaces_) {
        ui::Box hit{ws.box.x - cfg::workspace_pill_spacing / 2.0f, 0.0f, ws.box.w + cfg::workspace_pill_spacing, layout_.height};
        if (inside(hit, x, y)) {
            return {BarActionKind::workspace, BarItem::count, ws.id};
        }
    }
    return {};
}

bool BarModel::clickable(double x, double y) const {
    return press(x, y).kind != BarActionKind::none;
}

} // namespace astralia
