#include <algorithm>
#include <utility>

#include "render/icons.h"

#include "modules/bar/panel/bluetooth_panel.h"
#include "modules/bar/panel/widgets.h"

namespace astralia {

namespace {

namespace cfg = panel_config;

enum Action { power = 1,
              device,
              forget,
              cancel,
              confirm };

constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::text_px};

} // namespace

const char *bluetooth_kind_glyph(BluetoothDeviceKind) {
    return icon::bluetooth_device;
}

std::vector<BluetoothRow> bluetooth_rows(const BluetoothService &bluetooth) {
    const BluetoothStatus &status = bluetooth.status();
    if (!status.present) {
        return {{BluetoothRow::Kind::message, cfg::empty_height, "No adapter found"}};
    }
    if (!status.powered) {
        return {{BluetoothRow::Kind::message, cfg::empty_height, "Bluetooth is off"}};
    }
    std::vector<BluetoothRow> rows;
    auto bucket = [&](const char *title, auto belongs) {
        bool first = true;
        for (const BluetoothDevice &d : bluetooth.devices()) {
            if (!belongs(d)) {
                continue;
            }
            if (std::exchange(first, false)) {
                rows.push_back({BluetoothRow::Kind::section, cfg::section_height, title});
            }
            rows.push_back({BluetoothRow::Kind::device, cfg::device_row_height, d.name, &d});
        }
    };
    bucket("Connected", bluetooth_is_connected);
    bucket("Paired", bluetooth_is_paired);
    if (status.scanning) {
        bucket("Nearby", bluetooth_is_nearby);
    }
    if (rows.empty()) {
        rows.push_back({BluetoothRow::Kind::message, cfg::empty_height, status.scanning ? "Searching for devices\xE2\x80\xA6" : "No paired devices"});
    }
    rows.push_back({BluetoothRow::Kind::message, cfg::trailing_spacer, ""});
    return rows;
}

BluetoothPanel::BluetoothPanel(BluetoothService &bluetooth) : bluetooth_(bluetooth) {
    bluetooth_.changed.connect(notifier());
}

void BluetoothPanel::opened() {
    dialog_path_.clear();
    bluetooth_.start_discovery();
}

void BluetoothPanel::closed() {
    dialog_path_.clear();
    bluetooth_.stop_discovery();
}

float BluetoothPanel::content_height(ui::Canvas &) {
    float height = 0.0f;
    std::vector<BluetoothRow> rows = bluetooth_rows(bluetooth_);
    for (size_t i = 0; i < rows.size(); ++i) {
        height += (i > 0 ? cfg::list_spacing : 0.0f) + rows[i].height;
    }
    return height;
}

void BluetoothPanel::paint_header(ui::Canvas &canvas, const ui::Box &area, PanelPaint &paint) {
    if (!bluetooth_.status().present) {
        return;
    }
    ui::Box toggle{area.x + area.w - cfg::toggle_width, area.y + (area.h - cfg::toggle_height) / 2.0f, cfg::toggle_width, cfg::toggle_height};
    panel_widgets::toggle(canvas, toggle, bluetooth_.status().powered);
    paint.region(toggle, power);
}

void BluetoothPanel::paint(ui::Canvas &canvas, const ui::Box &view, float scroll, PanelPaint &paint) {
    std::vector<BluetoothRow> rows = bluetooth_rows(bluetooth_);
    float y = view.y - scroll;
    for (size_t i = 0; i < rows.size(); ++i) {
        const BluetoothRow &row = rows[i];
        if (i > 0) {
            y += cfg::list_spacing;
        }
        ui::Box box{view.x, y, view.w, row.height};
        y += row.height;
        if (box.y + box.h < view.y || box.y > view.y + view.h) {
            continue;
        }
        if (row.kind == BluetoothRow::Kind::message) {
            if (!row.text.empty()) {
                panel_widgets::centered_text(canvas, row.text, text_style, box, palette::text_dim);
            }
            continue;
        }
        if (row.kind == BluetoothRow::Kind::section) {
            ui::TextSize size = canvas.measure(row.text, text_style);
            canvas.text(row.text, text_style, box.x, box.y + box.h - size.h, palette::text_dim);
            continue;
        }
        const BluetoothDevice &d = *row.device;
        std::string subtitle = d.connecting ? "Connecting\xE2\x80\xA6" : d.battery >= 0 ? std::to_string(d.battery) + "%"
                                                                                        : std::string();
        panel_widgets::DeviceRow device_row;
        device_row.glyph = icon::bluetooth_device;
        device_row.title = d.name;
        device_row.subtitle = subtitle;
        device_row.background = d.connected ? palette::accent_alpha25 : d.connecting ? palette::accent_alpha12
                                                                                     : palette::text_alpha06;
        device_row.glyph_color = d.connected ? palette::text : palette::text_dim;
        device_row.title_color = d.connected ? palette::accent : palette::text;
        device_row.subtitle_color = palette::text_dim;
        device_row.connected = d.connected;
        device_row.busy = d.connecting;
        device_row.can_forget = (d.paired || d.trusted) && !d.connected && !d.connecting;
        int index = static_cast<int>(std::ranges::find(bluetooth_.devices(), d.path, &BluetoothDevice::path) - bluetooth_.devices().begin());
        panel_widgets::device_row(canvas, box, device_row, paint, device, forget, index);
    }
}

const BluetoothDevice *BluetoothPanel::dialog_device() const {
    if (dialog_path_.empty()) {
        return nullptr;
    }
    auto it = std::ranges::find(bluetooth_.devices(), dialog_path_, &BluetoothDevice::path);
    return it == bluetooth_.devices().end() ? nullptr : &*it;
}

float BluetoothPanel::dialog_height() {
    if (!dialog_path_.empty() && dialog_device() == nullptr) {
        dialog_path_.clear();
    }
    return dialog_path_.empty() ? 0.0f : cfg::confirm_dialog_height;
}

void BluetoothPanel::paint_dialog(ui::Canvas &canvas, const ui::Box &box, PanelPaint &paint) {
    const BluetoothDevice *target = dialog_device();
    if (target == nullptr) {
        return;
    }
    bool disconnecting = dialog_action_ == device;
    panel_widgets::confirm_dialog(canvas, box, target->name, disconnecting ? "Disconnect?" : "Forget?", disconnecting ? "Disconnect" : "Forget", paint, cancel, confirm);
}

bool BluetoothPanel::dismiss_dialog() {
    if (dialog_path_.empty()) {
        return false;
    }
    dialog_path_.clear();
    return true;
}

bool BluetoothPanel::activate(const PanelRegion &region, double, double) {
    const std::vector<BluetoothDevice> &devices = bluetooth_.devices();
    auto device_at = [&](int index) -> const BluetoothDevice * {
        return index >= 0 && static_cast<size_t>(index) < devices.size() ? &devices[static_cast<size_t>(index)] : nullptr;
    };
    switch (region.id) {
    case power:
        bluetooth_.set_powered(!bluetooth_.status().powered);
        return true;
    case device: {
        const BluetoothDevice *d = device_at(region.a);
        if (d == nullptr || d->connecting) {
            return true;
        }
        if (d->connected) {
            dialog_action_ = device;
            dialog_path_ = d->path;
        } else if (d->paired || d->trusted) {
            bluetooth_.connect(d->path);
        } else {
            bluetooth_.pair(d->path);
        }
        return true;
    }
    case forget:
        if (const BluetoothDevice *d = device_at(region.a)) {
            dialog_action_ = forget;
            dialog_path_ = d->path;
        }
        return true;
    case cancel:
        dialog_path_.clear();
        return true;
    case confirm:
        if (dialog_action_ == device) {
            bluetooth_.disconnect(dialog_path_);
        } else {
            bluetooth_.forget(dialog_path_);
        }
        dialog_path_.clear();
        return true;
    default:
        return false;
    }
}

bool BluetoothPanel::key(const input::KeyEvent &event) {
    return event.kind == input::KeyKind::Escape && dismiss_dialog();
}

} // namespace astralia
