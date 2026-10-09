#include <GLES3/gl32.h>
#include <algorithm>

#include "wayland/core/log.h"

#include "config/bar_layout.h"
#include "modules/bar/panel/battery_panel.h"
#include "modules/bar/panel/bluetooth_panel.h"
#include "modules/bar/panel/brightness_panel.h"
#include "modules/bar/panel/clock_panel.h"
#include "modules/bar/panel/media_panel.h"
#include "modules/bar/panel/network_panel.h"
#include "modules/bar/panel/resource_panel.h"
#include "modules/bar/panel/tray_panel.h"
#include "modules/bar/panel/volume_panel.h"

#include "modules/bar/wayland/bar.h"
#include "modules/bar/wayland/panel_surface.h"

#include "wayland/render/gl.h"

using astralia::PanelId;

PanelSurface::PanelSurface(WaylandState &app, MonitorOutput &mon) : app_(app), mon_(mon), set_(*app.reactor) {
    set_.add(PanelId::tray, std::make_unique<astralia::TrayPanel>(*app.tray));
    set_.add(PanelId::resource, std::make_unique<astralia::ResourcePanel>(app.cpu_temp, app.gpu_temp, app.system_stats));
    set_.add(PanelId::network, std::make_unique<astralia::NetworkPanel>(*app.network));
    set_.add(PanelId::bluetooth, std::make_unique<astralia::BluetoothPanel>(*app.bluetooth));
    set_.add(PanelId::volume, std::make_unique<astralia::VolumePanel>(*app.audio));
    set_.add(PanelId::battery, std::make_unique<astralia::BatteryPanel>(*app.battery));
    set_.add(PanelId::media, std::make_unique<astralia::MediaPanel>(*app.media));
    set_.add(PanelId::brightness, std::make_unique<astralia::BrightnessPanel>(*app.brightness_service));
    set_.add(PanelId::clock, std::make_unique<astralia::ClockPanel>());
    set_.on_changed = [this] { request_frame(); };
    set_.on_state = [this](PanelId id, bool open) { state_changed(id, open); };
}

PanelSurface::~PanelSurface() {
    if (text_focused_) {
        app_.text_input.clear_focused_client(this);
    }
}

float PanelSurface::card_top() const {
    return astralia::bar_layout::height + static_cast<float>(bar_style_of(mon_).top_margin) + astralia::bar_layout::panel_gap;
}

bool PanelSurface::create_surface(wl_output *output) {
    if (!overlay_panel_create_surface(base_, app_.compositor, app_.layer_shell, "astralia-shell-panel", output)) {
        klog("panel: failed to create layer surface on '%s'", mon_.output.name.c_str());
        return false;
    }
    return true;
}

bool PanelSurface::init_egl() {
    if (!base_.layer_surface) {
        return true;
    }
    if (!overlay_panel_init_egl(base_, app_.egl_display, app_.egl_config, app_.egl_context)) {
        return false;
    }
    canvas_.bind(app_.renderer);
    base_.frame_clock.draw = [this] { paint(); };
    app_detail::rest_egl_current(app_);
    return true;
}

void PanelSurface::destroy() {
    if (text_focused_) {
        app_.text_input.clear_focused_client(this);
        text_focused_ = false;
    }
    overlay_panel_destroy_surface(base_);
}

void PanelSurface::request_frame() {
    overlay_panel_request_frame(base_);
}

void PanelSurface::state_changed(PanelId, bool open) {
    if (!base_.layer_surface) {
        return;
    }
    if (open && !base_.open) {
        base_.open = true;
        zwlr_layer_surface_v1_set_keyboard_interactivity(base_.layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
        overlay_panel_update_input_region(base_);
        wl_surface_commit(base_.surface);
    } else if (!open && !set_.any_open()) {
        base_.open = false;
        zwlr_layer_surface_v1_set_keyboard_interactivity(base_.layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
        overlay_panel_update_input_region(base_);
        wl_surface_commit(base_.surface);
    }
    sync_text_input();
    if (open) {
        base_.open = true;
        request_frame();
    }
}

void PanelSurface::sync_text_input() {
    astralia::Panel *panel = set_.active();
    bool want = panel != nullptr && panel->content().wants_text();
    if (want == text_focused_) {
        return;
    }
    text_focused_ = want;
    if (want) {
        app_.text_input.set_focused_client(base_.surface, this);
    } else {
        app_.text_input.clear_focused_client(this);
    }
}

void PanelSurface::paint() {
    if (base_.egl_surface == EGL_NO_SURFACE) {
        return;
    }
    set_.tick(std::chrono::steady_clock::now());
    gl_make_current(base_.egl_display, base_.egl_surface, base_.egl_context);
    int32_t scale = base_.output_scale.scale;
    app_.renderer.begin_frame(base_.width, base_.height, scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    canvas_.begin(scale);
    if (base_.open) {
        set_.paint(canvas_, static_cast<float>(base_.width), card_top());
    }
    canvas_.flush();
    eglSwapBuffers(base_.egl_display, base_.egl_surface);
    sync_text_input();
    if (set_.animating()) {
        request_frame();
    }
}

void PanelSurface::press(int button, double x, double y) {
    astralia::input::Button mapped = button == 0x110 ? astralia::input::Button::Left : button == 0x111 ? astralia::input::Button::Right
                                                                                                       : astralia::input::Button::Other;
    if (astralia::Panel *panel = set_.active()) {
        panel->press(x, y, mapped);
    } else {
        set_.close_all();
    }
    request_frame();
}

void PanelSurface::wheel(double x, double y, double dy) {
    if (astralia::Panel *panel = set_.active()) {
        panel->wheel(x, y, dy);
    }
}

void PanelSurface::send_key(const astralia::input::KeyEvent &event) {
    if (astralia::Panel *panel = set_.active()) {
        panel->key(event);
        sync_text_input();
        request_frame();
    }
}

void PanelSurface::key(const KeyEvent &event) {
    send_key(to_neutral(event));
}

void PanelSurface::move(double x, double y) {
    if (astralia::Panel *panel = set_.active()) {
        panel->move(x, y);
    }
}

void PanelSurface::release() {
    if (astralia::Panel *panel = set_.active()) {
        panel->release();
    }
}

TextInputState PanelSurface::text_input_state() const {
    TextInputState state;
    state.purpose = TextInputPurpose::Password;
    const astralia::Panel *panel = set_.find(set_.active_id());
    if (panel != nullptr) {
        astralia::ui::Box c = panel->content().cursor();
        state.cursor_rect_x = static_cast<int32_t>(panel->dialog().x + c.x);
        state.cursor_rect_y = static_cast<int32_t>(panel->dialog().y + c.y);
        state.cursor_rect_w = std::max(1, static_cast<int32_t>(c.w));
        state.cursor_rect_h = std::max(1, static_cast<int32_t>(c.h));
    }
    return state;
}

void PanelSurface::text_input_apply_edit(const TextInputEdit &edit) {
    using astralia::input::KeyKind;
    astralia::input::KeyEvent event;
    if (edit.has_delete) {
        event.kind = KeyKind::Backspace;
        for (uint32_t i = 0; i < edit.delete_before_length; ++i) {
            send_key(event);
        }
    }
    if (edit.has_commit_text) {
        event = {};
        event.kind = KeyKind::Text;
        event.text = edit.commit_text;
        send_key(event);
    }
    if (edit.has_preedit) {
        event = {};
        event.kind = KeyKind::Preedit;
        event.text = edit.preedit_text;
        send_key(event);
    }
}

void PanelSurface::text_input_reset_preedit() {
    astralia::input::KeyEvent event;
    event.kind = astralia::input::KeyKind::Preedit;
    send_key(event);
}

void PanelSurface::text_input_deactivated(TextInputService &) {
    text_input_reset_preedit();
}
