#include <algorithm>
#include <cstdio>

#include "modules/bar/panel/battery_panel.h"
#include "modules/bar/panel/widgets.h"

namespace astralia {

namespace {

namespace cfg = panel_config;

constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::text_px};

} // namespace

std::string battery_time_text(int seconds) {
    if (seconds <= 0) {
        return "";
    }
    int hours = seconds / 3600;
    int minutes = seconds % 3600 / 60;
    char buffer[24];
    if (hours > 0) {
        std::snprintf(buffer, sizeof(buffer), "%dh %dm", hours, minutes);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%dm", minutes);
    }
    return buffer;
}

std::vector<BatteryRow> battery_rows(const BatteryService &battery) {
    std::vector<BatteryRow> rows;
    bool detected = false;
    std::vector<const BatteryDevice *> matched;
    for (const BatteryDevice &device : battery.devices()) {
        if (device.is_battery() && device.present) {
            matched.push_back(&device);
        }
        if (device.type == upower_type_battery && device.present) {
            detected = true;
        }
    }
    bool all_full = !battery.on_battery();
    for (const BatteryDevice *device : matched) {
        if (device->state != upower_state_fully_charged) {
            all_full = false;
            break;
        }
    }
    if (detected && all_full) {
        rows.push_back({BatteryRowKind::empty_plugged_in, cfg::battery_empty});
    }
    if (!all_full) {
        for (const BatteryDevice *device : matched) {
            rows.push_back({BatteryRowKind::device, cfg::battery_row, device});
        }
    }
    if (!detected) {
        rows.push_back({BatteryRowKind::empty_no_battery, cfg::battery_empty});
    }
    rows.push_back({BatteryRowKind::spacer, cfg::trailing_spacer});
    return rows;
}

BatteryPanel::BatteryPanel(BatteryService &battery) : battery_(battery) {
    battery_.changed.connect(notifier());
}

float BatteryPanel::content_height(ui::Canvas &) {
    float height = 0.0f;
    std::vector<BatteryRow> rows = battery_rows(battery_);
    for (size_t i = 0; i < rows.size(); ++i) {
        height += (i > 0 ? cfg::list_spacing : 0.0f) + rows[i].height;
    }
    return height;
}

void BatteryPanel::paint(ui::Canvas &canvas, const ui::Box &view, float scroll, PanelPaint &) {
    std::vector<BatteryRow> rows = battery_rows(battery_);
    float y = view.y - scroll;
    for (size_t i = 0; i < rows.size(); ++i) {
        const BatteryRow &row = rows[i];
        if (i > 0) {
            y += cfg::list_spacing;
        }
        ui::Box box{view.x, y, view.w, row.height};
        y += row.height;
        if (box.y + box.h < view.y || box.y > view.y + view.h) {
            continue;
        }
        if (row.kind == BatteryRowKind::empty_plugged_in || row.kind == BatteryRowKind::empty_no_battery) {
            panel_widgets::centered_text(canvas, row.kind == BatteryRowKind::empty_plugged_in ? "Plugged in" : "No Battery Detected", text_style, box, palette::text_dim);
            continue;
        }
        if (row.kind != BatteryRowKind::device) {
            continue;
        }
        const BatteryDevice &device = *row.device;
        bool charging = device.state == upower_state_charging;
        bool full = device.state == upower_state_fully_charged;
        bool pending = device.state == upower_state_pending_charge || (device.state == upower_state_discharging && !battery_.on_battery());
        const Color &bar_color = charging || full ? palette::accent : device.percent <= 15 ? palette::critical
                                                                  : device.percent <= 30   ? palette::warn
                                                                                           : palette::text_muted;
        std::string name = device.native_path.empty() ? "Battery" : device.native_path;
        const char *state = charging ? "Charging" : full  ? "Full"
                                                : pending ? "Pending"
                                                          : "Discharging";
        std::string time = battery_time_text(charging ? device.time_to_full_s : device.time_to_empty_s);
        ui::Box text_row{box.x, box.y, box.w, cfg::battery_text_row};
        ui::TextSize name_size = canvas.measure(name, text_style);
        panel_widgets::text_in_row(canvas, name, text_style, box.x, text_row, palette::text);
        std::string status = std::string(state) + (time.empty() ? "" : " \xE2\x80\x94 " + time);
        panel_widgets::text_in_row(canvas, status, text_style, box.x + name_size.w + cfg::tight_gap, text_row, charging ? palette::accent : palette::text_dim);

        std::string percent = std::to_string(device.percent) + "%";
        ui::TextSize pct_size = canvas.measure(percent, text_style);
        float bar_y = box.y + cfg::battery_text_row + cfg::battery_bar_top_gap;
        float bar_w = box.w - pct_size.w - cfg::content_gap;
        panel_widgets::flat_bar(canvas, {box.x, bar_y, bar_w, cfg::battery_bar_height}, static_cast<float>(std::min(device.percent, 100)) / 100.0f, cfg::battery_bar_height, palette::text_alpha08, bar_color);
        canvas.text(percent, text_style, box.x + bar_w + cfg::content_gap, bar_y + (cfg::battery_bar_height - pct_size.h) / 2.0f, bar_color);
    }
}

} // namespace astralia
