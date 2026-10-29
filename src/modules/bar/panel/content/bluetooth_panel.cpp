#include <algorithm>
#include <cctype>
#include <utility>

#include "render/icons.h"

#include "modules/bar/panel/content/bluetooth_panel.h"
#include "modules/bar/panel/core/widgets.h"

namespace astralia::bar {

namespace {

namespace cfg = panel_config;

enum Action { power = 1, device, forget, cancel, confirm, agent_yes, agent_cancel };

constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::text_px};
constexpr size_t passkey_max = 6;
constexpr size_t pin_max = 16;

bool input_kind(BluetoothAgentRequest::Kind kind) {
    return kind == BluetoothAgentRequest::Kind::input_passkey ||
           kind == BluetoothAgentRequest::Kind::input_pin;
}

} // namespace

const char *bluetooth_kind_glyph(BluetoothDeviceKind) { return icon::bluetooth_device; }

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
        rows.push_back(
            {BluetoothRow::Kind::message, cfg::empty_height,
             status.scanning ? "Searching for devices\xE2\x80\xA6" : "No paired devices"});
    }
    rows.push_back({BluetoothRow::Kind::message, cfg::trailing_spacer, ""});
    return rows;
}

BluetoothPanel::BluetoothPanel(BluetoothService &bluetooth) : bluetooth_(bluetooth) {
    bluetooth_.changed.connect(notifier());
}

void BluetoothPanel::opened() {
    dialog_path_.clear();
    code_ = {};
    bluetooth_.start_discovery();
}

void BluetoothPanel::closed() {
    dialog_path_.clear();
    code_ = {};
    bluetooth_.cancel_pairing();
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
    ui::Box toggle{area.x + area.w - cfg::toggle_width,
                   area.y + (area.h - cfg::toggle_height) / 2.0f, cfg::toggle_width,
                   cfg::toggle_height};
    widgets::toggle(canvas, toggle, bluetooth_.status().powered);
    paint.region(toggle, power);
}

void BluetoothPanel::paint(ui::Canvas &canvas, const ui::Box &view, float scroll,
                           PanelPaint &paint) {
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
                widgets::centered_text(canvas, row.text, text_style, box, palette::text_dim);
            }
            continue;
        }
        if (row.kind == BluetoothRow::Kind::section) {
            ui::TextSize size = canvas.measure(row.text, text_style);
            canvas.text(row.text, text_style, box.x, box.y + box.h - size.h, palette::text_dim);
            continue;
        }
        const BluetoothDevice &d = *row.device;
        std::string subtitle = d.connecting     ? "Connecting\xE2\x80\xA6"
                               : d.battery >= 0 ? std::to_string(d.battery) + "%"
                                                : std::string();
        widgets::DeviceRow device_row;
        device_row.glyph = icon::bluetooth_device;
        device_row.title = d.name;
        device_row.subtitle = subtitle;
        device_row.background = d.connected    ? palette::accent_alpha25
                                : d.connecting ? palette::accent_alpha12
                                               : palette::text_alpha06;
        device_row.glyph_color = d.connected ? palette::text : palette::text_dim;
        device_row.title_color = d.connected ? palette::accent : palette::text;
        device_row.subtitle_color = palette::text_dim;
        device_row.connected = d.connected;
        device_row.busy = d.connecting;
        device_row.can_forget = (d.paired || d.trusted) && !d.connected && !d.connecting;
        int index = static_cast<int>(
            std::ranges::find(bluetooth_.devices(), d.path, &BluetoothDevice::path) -
            bluetooth_.devices().begin());
        widgets::device_row(canvas, box, device_row, paint, device, forget, index);
    }
}

const BluetoothDevice *BluetoothPanel::dialog_device() const {
    if (dialog_path_.empty()) {
        return nullptr;
    }
    auto it = std::ranges::find(bluetooth_.devices(), dialog_path_, &BluetoothDevice::path);
    return it == bluetooth_.devices().end() ? nullptr : &*it;
}

bool BluetoothPanel::wants_text() const {
    const auto &request = bluetooth_.agent_request();
    return request && input_kind(request->kind);
}

ui::Box BluetoothPanel::cursor() const {
    return {width() / 2.0f,
            cfg::padding + cfg::dialog_spacer + (cfg::network_field_height - 14.0f) / 2.0f, 1.5f,
            14.0f};
}

void BluetoothPanel::submit_code() {
    bluetooth_.agent_input(code_.text);
    code_ = {};
}

float BluetoothPanel::dialog_height() {
    if (const auto &request = bluetooth_.agent_request()) {
        return input_kind(request->kind) ? cfg::field_dialog_height : cfg::pairing_dialog_height;
    }
    code_ = {};
    if (!dialog_path_.empty() && dialog_device() == nullptr) {
        dialog_path_.clear();
    }
    return dialog_path_.empty() ? 0.0f : cfg::confirm_dialog_height;
}

void BluetoothPanel::paint_dialog(ui::Canvas &canvas, const ui::Box &box, PanelPaint &paint) {
    if (const auto &request = bluetooth_.agent_request()) {
        using Kind = BluetoothAgentRequest::Kind;
        if (request->kind == Kind::confirm) {
            widgets::pairing_dialog(canvas, box, request->name, request->code,
                                    "Does this match the device?", paint, agent_cancel, agent_yes);
        } else if (request->kind == Kind::display_passkey) {
            std::string hint = "Type it on the device";
            if (request->entered > 0) {
                hint += " (" + std::to_string(request->entered) + "/" +
                        std::to_string(passkey_max) + ")";
            }
            widgets::pairing_dialog(canvas, box, request->name, request->code, hint, paint,
                                    agent_cancel);
        } else if (request->kind == Kind::display_pin) {
            widgets::pairing_dialog(canvas, box, request->name, request->code,
                                    "Type this PIN on the device", paint, agent_cancel);
        } else {
            widgets::dialog_top(canvas, box, request->name, paint, agent_cancel);
            float x = box.x + cfg::padding;
            float width = box.w - 2.0f * cfg::padding;
            float y = box.y + cfg::padding + cfg::dialog_spacer;
            ui::Box field{x, y, width, cfg::network_field_height};
            canvas.rounded(field, metrics::radius_widget, palette::text_alpha08, 1.0f,
                           palette::text_alpha15);
            if (code_.text.empty()) {
                widgets::centered_text(
                    canvas, request->kind == Kind::input_passkey ? "Passkey\xE2\x80\xA6"
                                                                 : "PIN\xE2\x80\xA6",
                    text_style, field, palette::text_dim);
            } else {
                widgets::centered_text(canvas, code_.text, text_style, field, palette::text);
            }
            y += cfg::network_field_height + cfg::row_gap;
            bool ready = !code_.text.empty();
            float button_w = canvas.measure("Pair", text_style).w + cfg::dialog_button_pad;
            ui::Box button{x + (width - button_w) / 2.0f, y, button_w, cfg::dialog_button};
            canvas.rounded(button, metrics::radius_widget,
                           with_alpha(palette::accent, ready ? 1.0f : 0.4f));
            widgets::centered_text(canvas, "Pair", text_style, button, palette::text);
            if (ready) {
                paint.region(button, agent_yes);
            }
        }
        return;
    }
    const BluetoothDevice *target = dialog_device();
    if (target == nullptr) {
        return;
    }
    bool disconnecting = dialog_action_ == device;
    widgets::confirm_dialog(canvas, box, target->name, disconnecting ? "Disconnect?" : "Forget?",
                            disconnecting ? "Disconnect" : "Forget", paint, cancel, confirm);
}

bool BluetoothPanel::dismiss_dialog() {
    if (bluetooth_.agent_request()) {
        code_ = {};
        bluetooth_.agent_answer(false);
        return true;
    }
    if (dialog_path_.empty()) {
        return false;
    }
    dialog_path_.clear();
    return true;
}

bool BluetoothPanel::activate(const PanelRegion &region, double, double) {
    const std::vector<BluetoothDevice> &devices = bluetooth_.devices();
    auto device_at = [&](int index) -> const BluetoothDevice * {
        return index >= 0 && static_cast<size_t>(index) < devices.size()
                   ? &devices[static_cast<size_t>(index)]
                   : nullptr;
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
    case agent_yes:
        if (wants_text()) {
            submit_code();
        } else {
            bluetooth_.agent_answer(true);
        }
        return true;
    case agent_cancel:
        return dismiss_dialog();
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
    if (wants_text()) {
        bool passkey = bluetooth_.agent_request()->kind == BluetoothAgentRequest::Kind::input_passkey;
        if (event.kind == input::KeyKind::Preedit) {
            return true;
        }
        input::KeyEvent typed = event;
        if (passkey && typed.kind == input::KeyKind::Text) {
            std::erase_if(typed.text, [](unsigned char c) { return !std::isdigit(c); });
            if (typed.text.empty()) {
                return true;
            }
        }
        switch (text_field_handle_key(code_, typed)) {
        case TextFieldResult::Committed:
            submit_code();
            return true;
        case TextFieldResult::Cancelled:
            return dismiss_dialog();
        case TextFieldResult::Changed:
            while (text_field_utf8_len(code_.text) > (passkey ? passkey_max : pin_max)) {
                text_field_backspace(code_.text);
            }
            return true;
        case TextFieldResult::None:
            return true;
        }
    }
    return event.kind == input::KeyKind::Escape && dismiss_dialog();
}

} // namespace astralia::bar
