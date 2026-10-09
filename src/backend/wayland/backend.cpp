#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <malloc.h>
#include <poll.h>
#include <span>
#include <sys/timerfd.h>
#include <unistd.h>
#include <vector>

#include "wayland/app/config.h"
#include "wayland/app/ipc.h"
#include "wayland/app/key_dispatch.h"
#include "wayland/app/module_registry.h"
#include "wayland/app/monitor_output.h"
#include "wayland/app/service_registry.h"
#include "wayland/app/wayland_registry.h"

#include "core/deferred_call.h"
#include "wayland/core/log.h"
#include "wayland/core/poll_source.h"

#include "core/animation.h"
#include "wayland/render/gl.h"

#include "app/backend.h"

#include "app/shell.h"
#include "core/dbus.h"
#include "core/ipc.h"
#include "core/poll_reactor.h"
#include "core/runtime_paths.h"

namespace {

Module *find_overlay_for_surface(WaylandState &app, wl_surface *surface) {
    if (!surface)
        return nullptr;
    for (auto &m : app.overlays)
        if (m->owns_surface(surface))
            return m.get();
    return nullptr;
}

class WaylandBackend final : public astralia::Backend {
  public:
    const char *name() const override { return "wayland"; }
    int malloc_arenas() const override { return 2; }
    int run() override;
};

int WaylandBackend::run() {
    mallopt(M_TRIM_THRESHOLD, 256 * 1024);
    bool want_lock = false;

    WaylandState &app = *new WaylandState;
    auto reactor = astralia::PollReactor::create();
    if (!reactor) {
        klog("%s", reactor.error().c_str());
        return 1;
    }

    app.reactor = &*reactor;
    app.session_bus = std::make_unique<astralia::SystemBus>(*reactor, astralia::BusKind::session);
    app.media = std::make_unique<astralia::MediaService>(*app.session_bus);
    app.audio = std::make_unique<astralia::AudioService>(*reactor);
    app.system_bus = std::make_unique<astralia::SystemBus>(*reactor);
    app.battery = std::make_unique<astralia::BatteryService>(*app.system_bus);
    app.network = std::make_unique<astralia::NetworkService>(*app.system_bus, *reactor);
    app.bluetooth = std::make_unique<astralia::BluetoothService>(*app.system_bus);
    app.tray = std::make_unique<astralia::TrayService>(*reactor);
    app.brightness_service = std::make_unique<astralia::BrightnessService>(*reactor);
    app.cfg = load_config();
    astralia::animation_set_instant(app.cfg.animations_disabled);
    app.config_watch_fd = config_watch_init(config_path());
    astralia::DeferredCall::attach(*reactor);

    app.display = wl_display_connect(nullptr);
    if (!app.display) {
        klog("failed to connect to Wayland display");
        return 1;
    }

    wl_registry *registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    wl_display_roundtrip(app.display);

    if (!app.compositor || !app.layer_shell || !app.wm_base) {
        klog("compositor is missing wl_compositor, zwlr_layer_shell_v1, or "
             "xdg_wm_base");
        return 1;
    }
    if (app.outputs.empty()) {
        klog("no wl_output advertised by the compositor");
        return 1;
    }

    if (!bootstrap_egl(app)) {
        klog("EGL init failed");
        return 1;
    }

    if (!renderer_bootstrap_init(app)) {
        klog("renderer init failed");
        return 1;
    }

    MonitorOutput &first = *app.outputs.front();
    monitor_output_create_surfaces(app, first);
    monitor_output_wait_configured(app, first);
    monitor_output_finish_egl(app, first);
    first.activated = true;

    app.overlays = build_app_modules(app.capabilities);
    for (auto &m : app.overlays) {
        if (!m->create_surface(app, first.output.wl))
            klog("overlay: failed to create layer surface");
    }

    for (;;) {
        bool all_configured = true;
        for (auto &m : app.overlays)
            if (!m->configured())
                all_configured = false;
        if (all_configured)
            break;
        wl_display_dispatch(app.display);
    }

    for (auto &m : app.overlays) {
        if (!m->init_egl(app)) {
            klog("overlay: EGL surface init failed");
            continue;
        }
        app_detail::rest_egl_current(app);
    }

    for (size_t i = 1; i < app.outputs.size(); ++i)
        monitor_output_activate(app, *app.outputs[i]);

    astralia::cpu_temp_init(app.cpu_temp);
    astralia::gpu_temp_init(app.gpu_temp, *app.reactor);

    app.services = build_services();
    for (auto &s : app.services)
        if (!s->init(app))
            klog("%s: init failed", s->name());

    if (want_lock && app.capabilities.lock)
        start_session_lock(app);

    auto rest_egl_current = [&app] { app_detail::rest_egl_current(app); };

    auto ipc = astralia::IpcServer::create(*reactor, astralia::runtime_path(".sock"));
    if (!ipc) {
        klog("%s", ipc.error().c_str());
        return 1;
    }
    astralia::Shell shell("wayland", app.capabilities);
    for (auto &m : app.overlays)
        shell.track(m->name(), [module = m.get()] { return module->is_open(); });
    shell.track("bar", [&app] {
        for (auto &mon : app.outputs)
            for (auto &pm : mon->modules)
                if (pm->is_open())
                    return true;
        return false;
    });
    shell.bind(collect_shell_bindings(app));
    shell.after_verb([&app, &rest_egl_current] {
        for (auto &m : app.overlays)
            m->request_frame();
        rest_egl_current();
        for (auto &mon : app.outputs)
            request_all_frames(*mon);
        rest_egl_current();
    });
    shell.attach(**ipc);

    klog("started: %zu monitor(s)", app.outputs.size());
    for (auto &mon : app.outputs)
        request_all_frames(*mon);

    struct SourceRange {
        PollSource *src;
        std::size_t start;
    };
    std::vector<FnPollSource> fn_sources;
    std::vector<PollSource *> raw_sources;
    std::vector<SourceRange> ranges;
    reactor->add_timer([] { return std::chrono::seconds(1); }, [&] {
        for (auto &mon : app.outputs)
            for (auto &m : mon->modules)
                m->timer_tick(app, *mon);
        for (auto &s : app.services)
            s->timer_tick(app);
        gl_poll_graphics_reset("timer");
        for (auto &m : app.overlays)
            if (m->timer_tick(app))
                rest_egl_current(); });

    reactor->add_poll_source(
        [&](std::vector<pollfd> &fds) {
            wl_display_flush(app.display);

            fn_sources.clear();
            raw_sources.clear();
            ranges.clear();

            for (auto &mon : app.outputs)
                for (auto &m : mon->modules)
                    m->tick(app, *mon);

            for (auto &s : app.services)
                for (auto &src : s->poll_sources(app))
                    fn_sources.push_back(std::move(src));

            for (auto &s : app.services)
                for (PollSource *src : s->raw_poll_sources(app))
                    raw_sources.push_back(src);

            if (app.config_watch_fd >= 0) {
                fn_sources.emplace_back(app.config_watch_fd, POLLIN, [&] {
                    ConfigWatchEvent ev = config_watch_poll(app.config_watch_fd);
                    if (ev.removed) {
                        close(app.config_watch_fd);
                        app.config_watch_fd = config_watch_init(config_path());
                    }
                    if (!ev.changed)
                        return;
                    if (app.config_own_write_pending) {
                        app.config_own_write_pending = false;
                        return;
                    }
                    app_detail::apply_config_update(app, load_config());
                });
            }

            for (auto &m : app.overlays)
                for (auto &[fd, cb] : m->extra_poll_sources(app))
                    fn_sources.emplace_back(fd, POLLIN, cb);

            if (app.keyboard.repeat_timer_fd >= 0) {
                fn_sources.emplace_back(app.keyboard.repeat_timer_fd, POLLIN, [&] {
                    keyboard_repeat_tick(app.keyboard);
                });
            }

            std::size_t base = fds.size();
            fds.push_back({.fd = wl_display_get_fd(app.display), .events = POLLIN, .revents = 0});
            for (FnPollSource &src : fn_sources) {
                std::size_t start = fds.size() - base;
                if (src.add_poll_fds(fds) > 0)
                    ranges.push_back({&src, start});
            }
            for (PollSource *src : raw_sources) {
                std::size_t start = fds.size() - base;
                if (src->add_poll_fds(fds) > 0)
                    ranges.push_back({src, start});
            }

            int poll_timeout_ms = -1;
            for (auto &m : app.overlays) {
                int t = m->poll_timeout_ms();
                if (t >= 0 && (poll_timeout_ms < 0 || t < poll_timeout_ms))
                    poll_timeout_ms = t;
            }
            return poll_timeout_ms;
        },
        [&](std::span<const pollfd> span) {
            std::vector<pollfd> fds(span.begin(), span.end());

            if (fds[0].revents & POLLIN) {
                wl_display_dispatch(app.display);

                if (app.pointer.dirty) {
                    app.pointer.dirty = false;
                    for (auto &mon : app.outputs)
                        request_all_frames(*mon);
                    for (auto &m : app.overlays)
                        m->handle_pointer_move(app, app.pointer.focused_surface, app.pointer.x, app.pointer.y);
                    if (app.pointer.focused_surface) {
                        if (MonitorOutput *m = find_monitor_for_surface(app, app.pointer.focused_surface))
                            app.last_pointer_monitor = m;
                    }
                    for (auto &mon : app.outputs)
                        for (auto &pm : mon->modules)
                            pm->handle_pointer_move(app, *mon, app.pointer.x, app.pointer.y);

                    Module *hovered =
                        find_overlay_for_surface(app, app.pointer.focused_surface);
                    bool hand = hovered && hovered->wants_pointing_hand_cursor();
                    if (!hand && app.pointer.focused_surface)
                        for (auto &mon : app.outputs)
                            for (auto &pm : mon->modules)
                                if (pm->owns_surface(app.pointer.focused_surface) && pm->wants_pointing_hand_cursor())
                                    hand = true;
                    pointer_set_cursor_shape(app.pointer, hand ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
                }
            }
            for (SourceRange &r : ranges)
                r.src->dispatch(fds, r.start);

            std::vector<KeyEvent> key_events = keyboard_drain_events(app.keyboard);
            dispatch_key_events(app, key_events);

            for (auto &m : app.overlays)
                if (m->tick()) {
                    m->request_frame();
                    rest_egl_current();
                }

            for (const PointerClick &click : pointer_drain_clicks(app.pointer)) {
                MonitorOutput *mon = find_monitor_for_surface(app, click.surface);
                if (!click.pressed) {
                    for (auto &m : app.outputs)
                        for (auto &pm : m->modules)
                            pm->handle_pointer_release();
                    for (auto &m : app.overlays)
                        m->handle_pointer_release();
                    continue;
                }
                if (click.button == BTN_LEFT) {
                    if (Module *m = find_overlay_for_surface(app, click.surface)) {
                        m->handle_click(app, click.x, click.y);
                        m->request_frame();
                        rest_egl_current();
                        continue;
                    }
                }
                if (!mon)
                    continue;
                for (auto &pm : mon->modules) {
                    if (pm->owns_surface(click.surface)) {
                        pm->handle_click(app, *mon, click.surface, click.button, click.x, click.y, click.serial);
                        break;
                    }
                }
            }

            for (const PointerScroll &scroll : pointer_drain_scrolls(app.pointer)) {
                MonitorOutput *mon = find_monitor_for_surface(app, scroll.surface);
                if (Module *m = find_overlay_for_surface(app, scroll.surface)) {
                    m->handle_scroll(app, scroll.dy);
                    continue;
                }
                if (!mon)
                    continue;
                for (auto &pm : mon->modules) {
                    if (pm->owns_surface(scroll.surface)) {
                        pm->handle_scroll(app, *mon, scroll.surface, scroll.dy);
                        break;
                    }
                }
            }

            if (!app.running)
                reactor->stop(EXIT_SUCCESS);
        });

    int code = reactor->run();
    return code;
}

} // namespace

extern "C" __attribute__((visibility("default"))) astralia::Backend *astralia_backend_create() { return new WaylandBackend(); }
