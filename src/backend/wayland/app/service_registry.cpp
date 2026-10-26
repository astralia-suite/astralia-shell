#include <chrono>
#include <cmath>
#include <unistd.h>

#include "wayland/app/module_registry.h"
#include "wayland/app/monitor_output.h"
#include "wayland/app/service_registry.h"
#include "wayland/app/wayland_state.h"
#include "wayland/core/log.h"

#include "modules/notification/wayland/notification.h"
#include "modules/osd/wayland/osd.h"
#include "modules/polkit/wayland/polkit.h"

#include "service/audio_service.h"
#include "service/battery_service.h"
#include "service/bluetooth_service.h"
#include "service/brightness_service.h"
#include "service/compositor_service.h"
#include "service/media_service.h"
#include "service/network_service.h"
#include "service/notification_service.h"
#include "service/polkit_service.h"
#include "service/theme_service.h"
#include "service/tray_service.h"
#include "service/wayland/idle_service.h"
#include "service/wayland/media_service.h"
#include "service/wayland/text_input_service.h"

namespace {

void redraw_all_monitors(WaylandState &app) {
    for (auto &mon : app.outputs)
        request_all_frames(*mon);
}

void redraw_and_present(WaylandState &app) {
    redraw_all_monitors(app);
    app_detail::rest_egl_current(app);
}

void network_dispatch(WaylandState &app, bool changed) {
    if (changed)
        redraw_and_present(app);
}

void notification_refresh(WaylandState &app) {
    app.notification.sync(*app.notifications);
    for (auto &mon : app.outputs)
        if (auto *nv = mon->module<astralia::notification::PerMonitor>())
            nv->request_frame();
}

void network_notify(WaylandState &app, const std::string &summary, const std::string &body) {
    app.notifications->post("Network", summary, body, 6000);
    notification_refresh(app);
}

void bluetooth_notify(WaylandState &app, const std::string &summary, const std::string &body) {
    app.notifications->post("Bluetooth", summary, body, 6000);
    notification_refresh(app);
}

class NotificationBusService final : public Service {
  public:
    const char *name() const override { return "notifications"; }

    bool init(WaylandState &app) override {
        app.notifications = std::make_unique<astralia::NotificationService>(*app.reactor);
        app.notifications->changed.connect([&app] { notification_refresh(app); });
        return true;
    }
};

class BrightnessOsdService final : public Service {
  public:
    const char *name() const override { return "brightness"; }

    bool init(WaylandState &app) override {
        app.brightness_service->changed.connect([&app] {
            astralia::osd::show_on_monitors(app, astralia::osd::OsdKind::brightness,
                                            app.brightness_service->percent(), false);
        });
        return true;
    }
};

class PipewireOsdService final : public Service {
  public:
    const char *name() const override { return "pipewire"; }

    bool init(WaylandState &app) override {
        app.audio->changed.connect([&app](astralia::AudioKind kind) {
            if (kind == astralia::AudioKind::sink) {
                astralia::AudioLevel level = app.audio->sink();
                astralia::osd::show_on_monitors(app, astralia::osd::OsdKind::volume,
                                                level.present ? level.percent : 0, level.muted);
            } else if (kind == astralia::AudioKind::source) {
                astralia::AudioLevel level = app.audio->source();
                astralia::osd::show_on_monitors(app, astralia::osd::OsdKind::mic,
                                                level.present ? level.percent : 0, level.muted);
            } else {
                return;
            }
            redraw_and_present(app);
        });
        return true;
    }
};

class UpowerService final : public Service {
  public:
    const char *name() const override { return "upower"; }

    bool init(WaylandState &app) override {
        app.battery->changed.connect([&app] { redraw_all_monitors(app); });
        return true;
    }
};

class NetworkService final : public Service {
  public:
    const char *name() const override { return "network"; }

    bool init(WaylandState &app) override {
        app.network->changed.connect([&app] { network_dispatch(app, true); });
        app.network->messages.connect([&app](const astralia::StatusMessage &message) {
            network_notify(app, message.summary, message.body);
        });
        app.reactor->add_timer([] { return std::chrono::milliseconds(400); },
                               [&app] {
                                   if (app.network->scanning() && app.network->wifi_enabled() &&
                                       astralia::network_visible_count(app.network->networks()) ==
                                           0)
                                       network_dispatch(app, true);
                               });
        return true;
    }
};

class BluetoothService final : public Service {
  public:
    const char *name() const override { return "bluetooth"; }

    bool init(WaylandState &app) override {
        app.bluetooth->changed.connect([&app] { redraw_and_present(app); });
        app.bluetooth->messages.connect([&app](const astralia::StatusMessage &message) {
            bluetooth_notify(app, message.summary, message.body);
        });
        return true;
    }
};

class TrayService final : public Service {
  public:
    const char *name() const override { return "tray"; }

    bool init(WaylandState &app) override {
        app.tray->changed.connect([&app] { redraw_and_present(app); });
        return true;
    }
};

class MprisService final : public Service {
  public:
    const char *name() const override { return "media"; }

    bool init(WaylandState &app) override {
        app.media->changed.connect([&app] {
            for (auto &m : app.overlays)
                if (m->is_open()) {
                    m->request_frame();
                    app_detail::rest_egl_current(app);
                }
        });
        return true;
    }
};

class CompositorWorkspaceService final : public Service {
  public:
    const char *name() const override { return "compositor-workspace"; }

    bool init(WaylandState &app) override {
        app.desktop = astralia::make_compositor(*app.reactor);
        app.desktop->watch_client_order(true);
        app_detail::sync_compositor_borders(app);
        app.desktop->active_changed.connect([&app] { redraw_all_monitors(app); });
        app.desktop->structure_changed.connect([&app] {
            redraw_all_monitors(app);
            for (auto &m : app.overlays)
                if (m->is_open()) {
                    m->request_frame();
                    app_detail::rest_egl_current(app);
                }
        });
        return true;
    }
};

class ThemeFollowService final : public Service {
  public:
    const char *name() const override { return "theme"; }

    bool init(WaylandState &app) override {
        app.theme = std::make_unique<astralia::ThemeService>();
        app.theme->decode_video_frame = [](const std::string &path, int &w, int &h) {
            return animate_decode_scaled(path, 256, 256, w, h);
        };
        app.theme->changed.connect([&app] {
            app_detail::sync_compositor_borders(app);
            redraw_all_monitors(app);
            for (auto &m : app.overlays)
                if (m->is_open())
                    m->request_frame();
            app_detail::rest_egl_current(app);
        });
        app_detail::theme_sync(app);
        return true;
    }
};

class PolkitBusService final : public Service {
  public:
    const char *name() const override { return "polkit"; }

    bool init(WaylandState &app) override {
        app.polkit = std::make_unique<astralia::PolkitService>(*app.reactor);
        app.polkit->changed.connect([&app] { astralia::polkit::notify_state_changed(app); });
        app.polkit->ready.connect([](bool ok, const std::string &error) {
            if (ok)
                klog("polkit: agent registered");
            else
                klog("polkit: agent registration failed: %s", error.c_str());
        });
        return true;
    }
};

class TextInputProtocolService final : public Service {
  public:
    const char *name() const override { return "text-input"; }

    bool init(WaylandState &app) override {
        app.keyboard.on_focus_surface = [&app](wl_surface *surface, bool entered) {
            app.text_input.on_keyboard_focus_surface(surface, entered);
        };
        if (!app.text_input.bind(app.text_input_manager, app.seat))
            klog("text-input: zwp_text_input_manager_v3 unavailable - IME "
                 "input unavailable");
        return true;
    }
};

class ClipboardProtocolService final : public Service {
  public:
    const char *name() const override { return "clipboard"; }

    bool init(WaylandState &app) override {
        if (!app.clipboard.bind(app.data_device_manager, app.seat, app.display, &app.keyboard))
            klog("clipboard: wl_data_device_manager unavailable - copy and "
                 "paste unavailable");
        return true;
    }

    std::vector<PollSource *> raw_poll_sources(WaylandState &app) override {
        read_source_.clipboard = &app.clipboard;
        return {&read_source_};
    }

  private:
    struct ReadSource final : PollSource {
        ClipboardService *clipboard = nullptr;

        std::size_t add_poll_fds(std::vector<pollfd> &fds) override {
            if (!clipboard || clipboard->read_fd() < 0)
                return 0;
            fds.push_back({.fd = clipboard->read_fd(), .events = POLLIN, .revents = 0});
            return 1;
        }

        void dispatch(const std::vector<pollfd> &fds, std::size_t start) override {
            if (fds[start].revents & (POLLIN | POLLHUP | POLLERR))
                clipboard->read_ready();
        }
    };

    ReadSource read_source_;
};

class IdleService final : public Service {
  public:
    const char *name() const override { return "idle"; }

    bool init(WaylandState &app) override { return idle_init(app.idle, app.seat); }

    void timer_tick(WaylandState &app) override {
        idle_tick(app.idle, app.desktop->state().focused_monitor);
    }
};

} // namespace

std::vector<std::unique_ptr<Service>> build_services() {
    std::vector<std::unique_ptr<Service>> services;
    services.push_back(std::make_unique<NotificationBusService>());
    services.push_back(std::make_unique<BrightnessOsdService>());
    services.push_back(std::make_unique<PipewireOsdService>());
    services.push_back(std::make_unique<UpowerService>());
    services.push_back(std::make_unique<NetworkService>());
    services.push_back(std::make_unique<BluetoothService>());
    services.push_back(std::make_unique<TrayService>());
    services.push_back(std::make_unique<MprisService>());
    services.push_back(std::make_unique<PolkitBusService>());
    services.push_back(std::make_unique<ThemeFollowService>());
    services.push_back(std::make_unique<CompositorWorkspaceService>());
    services.push_back(std::make_unique<TextInputProtocolService>());
    services.push_back(std::make_unique<ClipboardProtocolService>());
    services.push_back(std::make_unique<IdleService>());
    return services;
}
