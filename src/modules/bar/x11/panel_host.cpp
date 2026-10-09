#include <algorithm>
#include <cstdlib>

#include "core/pointer.h"

#include "modules/bar/panel/battery_panel.h"
#include "modules/bar/panel/bluetooth_panel.h"
#include "modules/bar/panel/brightness_panel.h"
#include "modules/bar/panel/clock_panel.h"
#include "modules/bar/panel/media_panel.h"
#include "modules/bar/panel/network_panel.h"
#include "modules/bar/panel/tray_panel.h"
#include "modules/bar/panel/volume_panel.h"
#include "modules/bar/x11/panel_host.h"

namespace astralia {

namespace {

constexpr uint16_t backing_width = 520;
constexpr uint16_t backing_height = 940;
constexpr double wheel_step = 40.0;

} // namespace

PanelHost::PanelHost(XConnection &x, EventLoop &loop, Services &services)
    : x_(x), keyboard_(x.conn()), set_(loop),
      window_(x, "astralia-panel",
              XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_FOCUS_CHANGE),
      popup_(x, "astralia-panel-popup", XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_LEAVE_WINDOW) {
    output_ = x.primary_output();
    set_.add(PanelId::tray, std::make_unique<TrayPanel>(services.tray));
    set_.add(PanelId::network, std::make_unique<NetworkPanel>(services.network));
    set_.add(PanelId::bluetooth, std::make_unique<BluetoothPanel>(services.bluetooth));
    set_.add(PanelId::volume, std::make_unique<VolumePanel>(services.audio));
    set_.add(PanelId::battery, std::make_unique<BatteryPanel>(services.battery));
    set_.add(PanelId::media, std::make_unique<MediaPanel>(services.media));
    set_.add(PanelId::brightness, std::make_unique<BrightnessPanel>(services.brightness));
    set_.add(PanelId::clock, std::make_unique<ClockPanel>());
    set_.on_changed = [this] {
        if (window_.mapped()) {
            paint();
        }
    };
    set_.on_state = [this](PanelId id, bool open) { state_changed(id, open); };
    loop.on_window(window_.id(), [this](const xcb_generic_event_t &event) { handle(event); });
    loop.on_window(popup_.id(), [this](const xcb_generic_event_t &event) { handle_popup(event); });
}

void PanelHost::set_output(const OutputGeometry &output, int top) {
    output_ = output;
    top_ = top;
}

void PanelHost::state_changed(PanelId, bool open) {
    if (open && !window_.mapped()) {
        keyboard_.reload();
        paint();
        window_.show(true);
        window_.present();
        grab();
    } else if (!open && !set_.any_open()) {
        hide_popup();
        ungrab();
        window_.hide();
        window_.release();
    }
    changed.emit();
}

void PanelHost::grab() {
    if (grabbed_) {
        return;
    }
    constexpr uint16_t mask = XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_1_MOTION;
    xcb_grab_pointer_reply_t *reply = xcb_grab_pointer_reply(
        x_.conn(), xcb_grab_pointer(x_.conn(), 1, window_.id(), mask, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME), nullptr);
    grabbed_ = reply && reply->status == XCB_GRAB_STATUS_SUCCESS;
    free(reply);
}

void PanelHost::ungrab() {
    if (grabbed_) {
        xcb_ungrab_pointer(x_.conn(), XCB_CURRENT_TIME);
        grabbed_ = false;
    }
}

void PanelHost::paint() {
    Panel *panel = set_.active();
    if (panel == nullptr) {
        return;
    }
    float x = panel->card_x(static_cast<float>(output_.width));
    OutputGeometry geometry{static_cast<int16_t>(output_.x + static_cast<int>(x)), static_cast<int16_t>(output_.y + top_), static_cast<uint16_t>(panel->content().width()), 1};
    if (window_.cr() == nullptr) {
        geometry.height = backing_height;
        window_.place(geometry, backing_width, backing_height);
    }
    canvas_.bind(window_.cr());
    window_.clear();
    panel->paint_at(canvas_, 0.0f, 0.0f);
    ui::Box extent = panel->extent();
    geometry.height = static_cast<uint16_t>(std::clamp(static_cast<int>(extent.h), 1, static_cast<int>(backing_height)));
    if (window_.geometry() != geometry) {
        window_.place(geometry, backing_width, backing_height);
        canvas_.bind(window_.cr());
    }
    window_.present();
    sync_popup();
}

void PanelHost::hide_popup() {
    if (popup_.mapped()) {
        popup_.hide();
    }
    popup_.release();
}

void PanelHost::sync_popup() {
    Panel *panel = set_.active();
    if (panel == nullptr || !panel->popup_wanted()) {
        hide_popup();
        return;
    }
    auto width = static_cast<int>(panel->popup_width());
    int height = std::clamp(static_cast<int>(panel->popup_height()), 1, static_cast<int>(output_.height));
    ui::Box anchor = panel->popup_anchor();
    const OutputGeometry &parent = window_.geometry();
    int x = std::clamp(static_cast<int>(parent.x + anchor.x), static_cast<int>(output_.x), std::max(static_cast<int>(output_.x), static_cast<int>(output_.x + output_.width) - width));
    int y = static_cast<int>(parent.y + anchor.y + anchor.h);
    if (y + height > output_.y + output_.height) {
        y = std::max(static_cast<int>(output_.y), static_cast<int>(parent.y + anchor.y) - height);
    }
    OutputGeometry geometry{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<uint16_t>(width), static_cast<uint16_t>(height)};
    popup_.place(geometry, geometry.width, geometry.height);
    popup_canvas_.bind(popup_.cr());
    popup_.clear();
    panel->paint_popup(popup_canvas_);
    popup_.show(false);
    popup_.present();
}

void PanelHost::handle_popup(const xcb_generic_event_t &event) {
    Panel *panel = set_.active();
    switch (event.response_type & ~0x80) {
    case XCB_EXPOSE:
        if (popup_.mapped()) {
            popup_.present();
        }
        break;
    case XCB_BUTTON_PRESS: {
        const auto &press = reinterpret_cast<const xcb_button_press_event_t &>(event);
        if (panel == nullptr || scroll_event(press.detail)) {
            break;
        }
        input::PointerEvent pointer = pointer_event(press.event_x, press.event_y, press.detail, true);
        panel->press_popup(pointer.x, pointer.y, pointer.button);
        break;
    }
    case XCB_MOTION_NOTIFY: {
        const auto &motion = reinterpret_cast<const xcb_motion_notify_event_t &>(event);
        if (panel != nullptr) {
            panel->hover_popup(motion.event_x, motion.event_y);
        }
        break;
    }
    case XCB_LEAVE_NOTIFY:
        if (panel != nullptr) {
            panel->hover_popup(-1, -1);
        }
        break;
    default:
        break;
    }
}

void PanelHost::handle(const xcb_generic_event_t &event) {
    Panel *panel = set_.active();
    switch (event.response_type & ~0x80) {
    case XCB_EXPOSE:
        if (window_.mapped()) {
            window_.present();
            grab();
        }
        break;
    case XCB_BUTTON_PRESS: {
        const auto &press = reinterpret_cast<const xcb_button_press_event_t &>(event);
        if (panel == nullptr) {
            break;
        }
        if (auto scroll = scroll_event(press.detail)) {
            panel->wheel(press.event_x, press.event_y, scroll->dy * wheel_step);
            break;
        }
        input::PointerEvent pointer = pointer_event(press.event_x, press.event_y, press.detail, true);
        panel->press(pointer.x, pointer.y, pointer.button);
        break;
    }
    case XCB_BUTTON_RELEASE:
        if (panel != nullptr) {
            panel->release();
        }
        break;
    case XCB_MOTION_NOTIFY: {
        const auto &motion = reinterpret_cast<const xcb_motion_notify_event_t &>(event);
        if (panel != nullptr && !panel->move(motion.event_x, motion.event_y)) {
            panel->hover(motion.event_x, motion.event_y);
        }
        break;
    }
    case XCB_KEY_PRESS: {
        const auto &key = reinterpret_cast<const xcb_key_press_event_t &>(event);
        if (panel != nullptr) {
            panel->key(to_neutral(keyboard_.press(key.detail, key.state)));
        }
        break;
    }
    case XCB_FOCUS_OUT: {
        const auto &focus = reinterpret_cast<const xcb_focus_out_event_t &>(event);
        if (focus.mode == XCB_NOTIFY_MODE_NORMAL && focus.detail != XCB_NOTIFY_DETAIL_POINTER) {
            set_.close_all();
        }
        break;
    }
    default:
        break;
    }
}

} // namespace astralia
