#pragma once

#include <EGL/egl.h>
#include <cstring>
#include <memory>
#include <vector>
#include <wayland-client.h>

#include "app/shell.h"

#include "wayland/app/config.h"
#include "wayland/app/module.h"
#include "wayland/app/service.h"

#include "modules/notification/wayland/notification.h"

#include "wayland/render/renderer.h"

#include "core/dbus.h"
#include "service/bluetooth_service.h"
#include "service/brightness_service.h"
#include "service/compositor_service.h"
#include "service/wayland/idle_service.h"
#include "service/wayland/input_service.h"

#include "service/audio_service.h"
#include "service/battery_service.h"
#include "service/media_service.h"
#include "service/network_service.h"
#include "service/notification_service.h"
#include "service/polkit_service.h"
#include "service/telemetry_service.h"
#include "service/tray_service.h"
#include "service/wayland/text_input_service.h"

#include "ext-session-lock-v1-client-protocol.h"
#include "hyprland-toplevel-export-v1-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

struct MonitorOutput;

struct WaylandState {
    astralia::Capabilities capabilities = astralia::wayland_capabilities();
    wl_display *display = nullptr;
    wl_compositor *compositor = nullptr;
    zwlr_layer_shell_v1 *layer_shell = nullptr;
    xdg_wm_base *wm_base = nullptr;
    wl_seat *seat = nullptr;
    wl_shm *shm = nullptr;
    hyprland_toplevel_export_manager_v1 *toplevel_export_manager = nullptr;
    zwp_text_input_manager_v3 *text_input_manager = nullptr;
    ext_session_lock_manager_v1 *session_lock_manager = nullptr;
    TextInputService text_input;
    EGLDisplay egl_display = EGL_NO_DISPLAY;
    EGLConfig egl_config = nullptr;
    EGLContext egl_context = EGL_NO_CONTEXT;
    std::vector<EGLint> egl_context_attribs;
    EGLSurface egl_rest_surface = EGL_NO_SURFACE;
    Config cfg;
    bool running = true;
    bool session_locked = false;
    Renderer renderer;
    IdleState idle;
    astralia::NotificationModel notification;
    std::unique_ptr<astralia::NotificationService> notifications;
    std::vector<std::unique_ptr<Module>> overlays;
    std::vector<std::unique_ptr<Service>> services;
    std::unique_ptr<astralia::SystemBus> system_bus;
    std::unique_ptr<astralia::BatteryService> battery;
    std::unique_ptr<astralia::NetworkService> network;
    std::unique_ptr<astralia::BluetoothService> bluetooth;
    std::unique_ptr<astralia::TrayService> tray;
    KeyboardState keyboard;
    PointerState pointer;
    SeatCapabilityState seat_caps;
    std::unique_ptr<astralia::BrightnessService> brightness_service;
    std::unique_ptr<astralia::AudioService> audio;
    astralia::CpuTempState cpu_temp;
    astralia::GpuTempState gpu_temp;
    astralia::SystemStatsState system_stats;
    std::unique_ptr<astralia::SystemBus> session_bus;
    std::unique_ptr<astralia::MediaService> media;
    std::unique_ptr<astralia::PolkitService> polkit;
    int config_watch_fd = -1;
    bool config_own_write_pending = false;
    MonitorOutput *last_pointer_monitor = nullptr;
    std::unique_ptr<astralia::Compositor> desktop = std::make_unique<astralia::NullCompositor>();
    astralia::Reactor *reactor = nullptr;
    std::vector<std::unique_ptr<MonitorOutput>> outputs;
};

inline bool compositor_available(const WaylandState &app) { return std::strcmp(app.desktop->name(), "none") != 0; }

inline Module *find_overlay_by_name(WaylandState &app, const char *name) {
    for (auto &m : app.overlays)
        if (strcmp(m->name(), name) == 0)
            return m.get();
    return nullptr;
}
