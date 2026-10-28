#include <algorithm>
#include <ctime>

#include "config/bar_layout.h"

#include "core/wall_clock.h"

#include "render/glyphs.h"
#include "render/icons.h"

#include "modules/bar/model/model.h"

namespace astralia::bar {

namespace {

namespace cfg = bar_layout;

bool inside(const ui::Box &box, double x, double y) {
    return box.w > 0.0f && x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h;
}

} // namespace

const char *wifi_glyph(int strength) {
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

const char *bluetooth_glyph(const BluetoothStatus &status) {
    if (!status.powered) {
        return icon::bluetooth_off;
    }
    return status.connected ? icon::bluetooth_connected : icon::bluetooth_on;
}

std::string bluetooth_label(const BluetoothStatus &status) {
    if (!status.present) {
        return "";
    }
    if (!status.powered) {
        return "Disconnected";
    }
    return status.connected && !status.device.empty() ? status.device : "Idle";
}

const char *battery_glyph(const BatteryStatus &status) {
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

std::string battery_label(const BatteryStatus &status) {
    return status.full ? "Plugged in" : std::to_string(status.percent) + "%";
}

Model::Model(Sources sources, Clock::time_point now)
    : sources_(std::move(sources)), style_(&style_spec(BarStyle::continuous, false)),
      started_(now) {}

void Model::refresh(Clock::time_point now) {
    auto set = [&](Item id, const char *glyph, std::string label, bool visible,
                   bool retracts = true) {
        ItemView &view = mut(id);
        view.retracts = retracts;
        view.glyph = visible ? glyph : nullptr;
        view.label = visible ? std::move(label) : std::string();
        view.detail.clear();
        view.visible = visible && glyph != nullptr;
        view.tint.reset();
        view.alert = false;
    };
    set(Item::logout, icon::power, "Logout", true);
    set(Item::tray, icon::tray, "Tray", true);
    set(Item::resource, icon::cpu, "Resource", sources_.resource);
    set(Item::media, icon::music_note, "Media", true);
    std::tm local = wall_local_time();
    auto format = [&](const char *pattern) {
        char buf[32];
        return std::string(buf, std::strftime(buf, sizeof(buf), pattern, &local));
    };
    set(Item::time, icon::clock, format("%H:%M:%S"), true, false);
    set(Item::date, icon::calendar, format("%a %Y-%m-%d"), true);
    set(Item::notifications, icon::bell, "Notifications", sources_.notifications != nullptr);
    if (sources_.notifications != nullptr && !sources_.notifications->list().empty()) {
        mut(Item::notifications).tint = palette::accent;
    }
    refresh_active_window();

    if (sources_.network != nullptr) {
        const NetworkService &net = *sources_.network;
        bool portal = net.connectivity() == "portal" || net.status().portal;
        const char *glyph = icon::wifi_off;
        std::string label;
        if (portal) {
            glyph = icon::lock;
            label = "Sign in";
        } else if (net.status().kind == NetworkKind::ethernet || net.ethernet_connected()) {
            glyph = icon::router;
            label = "Ethernet";
        } else if (net.status().kind == NetworkKind::wifi) {
            glyph = wifi_glyph(net.status().strength);
            label = net.status().ssid;
        } else if (net.ethernet_available() && !net.wifi_available()) {
            glyph = icon::router;
        }
        set(Item::network, glyph, label.empty() ? "Wi-Fi" : label, true);
    } else {
        set(Item::network, nullptr, "", false);
    }

    if (sources_.bluetooth != nullptr && sources_.bluetooth->status().present) {
        const BluetoothStatus &status = sources_.bluetooth->status();
        const char *glyph = bluetooth_glyph(status);
        std::string label = bluetooth_label(status);
        set(Item::bluetooth, glyph, label, true);
    } else {
        set(Item::bluetooth, nullptr, "", false);
    }

    if (sources_.audio != nullptr && sources_.audio->sink().present) {
        AudioLevel level = sources_.audio->sink();
        set(Item::volume, icon::volume_threshold(level.muted, level.percent),
            level.muted ? "muted" : std::to_string(level.percent) + "%", true);
        if (!peek_ready_ && now - started_ >= cfg::peek_ready_delay) {
            peek_ready_ = true;
        }
        if (peek_ready_) {
            bool changed =
                peek_level_ < 0 || peek_level_ != level.percent || peek_muted_ != level.muted;
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
        set(Item::volume, nullptr, "", false);
    }

    if (sources_.brightness != nullptr && sources_.brightness->available(sources_.output)) {
        int percent = sources_.brightness->percent(sources_.output);
        set(Item::brightness, icon::brightness_threshold(percent), std::to_string(percent) + "%",
            true);
    } else {
        set(Item::brightness, nullptr, "", false);
    }

    if (sources_.battery != nullptr && sources_.battery->status().present) {
        const BatteryStatus &status = sources_.battery->status();
        set(Item::battery, battery_glyph(status), battery_label(status), true);
        ItemView &view = mut(Item::battery);
        if (status.full) {
            view.tint.reset();
        } else {
            view.tint = status.charging        ? palette::accent_alt
                        : status.percent <= 25 ? palette::critical_container
                                               : palette::accent;
        }
        view.alert =
            !status.charging && !status.full && status.percent <= cfg::battery_critical_percent;
    } else {
        set(Item::battery, nullptr, "", false);
    }

    refresh_workspaces();
    refresh_dock();
    apply_hover(now);
}

void Model::refresh_active_window() {
    if (!sources_.compositor) {
        set_hidden(Item::active_window);
        return;
    }
    const CompositorState &state = sources_.compositor();
    auto monitor = state.by_monitor.find(sources_.output);
    int active = monitor != state.by_monitor.end() ? monitor->second.active_id : -1;
    ItemView &view = mut(Item::active_window);
    view.glyph = icon::app_window;
    view.visible = true;
    view.detail.clear();
    view.label = "Workspace " + std::to_string(active > 0 ? active : 1);
    for (const CompositorClient &client : state.clients) {
        if (client.focus_history_id == 0 && client.workspace_id == active) {
            view.label = client.initial_title.empty() ? client.window_class : client.initial_title;
            view.detail = client.title;
            break;
        }
    }
}

void Model::set_hidden(Item id) {
    ItemView &view = mut(id);
    view = {};
}

bool Model::update_expand(Item id, bool target, bool instant) {
    ItemView &view = mut(id);
    if (view.hovered == target) {
        return false;
    }
    view.hovered = target;
    size_t index = static_cast<size_t>(id);
    animations_.animate(
        view.expand, target ? 1.0f : 0.0f, instant ? 0.0f : cfg::expand_ms,
        astralia::Easing::EaseOutCubic, [this, index](float v) { items_[index].expand = v; }, {},
        cfg::owner_expand + index);
    return true;
}

bool Model::apply_hover(Clock::time_point now) {
    bool changed = false;
    std::optional<Item> target;
    if (open_) {
        target = open_;
    } else if (linger_ && now < linger_until_) {
        target = linger_;
    } else {
        target = hovered_;
    }
    if (!target && peek_active_) {
        target = Item::volume;
    }
    for (size_t i = 0; i < item_count; ++i) {
        Item id = static_cast<Item>(i);
        const ItemView &view = items_[i];
        bool want = view.visible && (target == id || !view.retracts) && !view.label.empty();
        changed |= update_expand(id, want, want && open_ == id);
    }
    return changed;
}

void Model::set_open(std::optional<Item> item, Clock::time_point now) {
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

bool Model::hover(std::optional<double> x, std::optional<double> y, Clock::time_point now) {
    pointer_x_ = x;
    pointer_y_ = y;
    hovered_.reset();
    if (x && y) {
        for (size_t i = 0; i < item_count; ++i) {
            const ItemView &view = items_[i];
            if (view.visible && inside(view.box, *x, *y)) {
                hovered_ = static_cast<Item>(i);
                break;
            }
        }
    }
    return apply_hover(now);
}

bool Model::tick(Clock::time_point now) {
    if (peek_active_ && now >= peek_deadline_) {
        peek_active_ = false;
    }
    animations_.tick(now);
    return apply_hover(now);
}

std::chrono::milliseconds Model::until_idle(Clock::time_point now) const {
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
    if (animations_.has_active()) {
        consider(now + std::chrono::milliseconds(16));
    }
    return wait;
}

Frame Model::frame() const {
    return bar_frame(*style_, layout_.left_end, layout_.center, layout_.right_start, layout_.width);
}

Action Model::press(double x, double y) const {
    for (size_t i = 0; i < item_count; ++i) {
        const ItemView &view = items_[i];
        Item id = static_cast<Item>(i);
        if (id == Item::notifications) {
            continue;
        }
        if (view.visible && inside(view.box, x, y)) {
            if (id == Item::active_window) {
                return {ActionKind::overview};
            }
            return {ActionKind::toggle, id, 0};
        }
    }
    for (const BarWorkspace &ws : workspaces_) {
        float gap = style_->workspace_gap;
        ui::Box hit{ws.box.x - gap / 2.0f, 0.0f, ws.box.w + gap, layout_.height};
        if (inside(hit, x, y)) {
            return {ActionKind::workspace, Item::count, ws.id};
        }
    }
    return {};
}

bool Model::clickable(double x, double y) const { return press(x, y).kind != ActionKind::none; }

} // namespace astralia::bar
