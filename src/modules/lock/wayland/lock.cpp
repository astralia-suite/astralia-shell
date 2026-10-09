#include <GLES3/gl32.h>
#include <cctype>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

#include "wayland/app/monitor_output.h"
#include "wayland/app/user_info.h"
#include "wayland/app/wayland_state.h"

#include "core/deferred_call.h"
#include "wayland/core/log.h"

#ifndef ASTRALIA_SHELL_PAM_DIR
#define ASTRALIA_SHELL_PAM_DIR ""
#endif

#include "modules/lock/layout.h"
#include "modules/lock/pam_authenticator.h"
#include "modules/lock/view.h"
#include "modules/lock/wayland/lock.h"

#include "render/tokens.h"
#include "wayland/render/gl.h"

#include "service/telemetry_service.h"

namespace {

void lock_paint(LockState &st, LockOutputSurface &los);
void finish_unlock(LockState &st);

LockOutputSurface *surface_for(LockState &st, wl_surface *s) {
    for (auto &up : st.surfaces)
        if (up->surface == s)
            return up.get();
    return nullptr;
}

void request_all(LockState &st) {
    for (auto &up : st.surfaces)
        if (up->frame_clock.surface)
            request_frame(up->frame_clock);
    if (st.app)
        app_detail::rest_egl_current(*st.app);
}

std::string clock_text(const char *format) {
    std::time_t now = std::time(nullptr);
    char buffer[64];
    size_t n = std::strftime(buffer, sizeof(buffer), format, std::localtime(&now));
    return std::string(buffer, n);
}

astralia::LockInfo lock_info(const LockState &st) {
    WaylandState &app = *st.app;
    std::string date = clock_text("%a %Y-%m-%d");
    for (char &c : date)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return {st.user,
            user_info::os_pretty_name(),
            app.desktop->name(),
            user_info::uptime_string(),
            clock_text("%H"),
            clock_text("%M"),
            date,
            &app.battery->status(),
            &app.media->status(),
            &app.system_stats,
            &app.cpu_temp,
            &app.gpu_temp,
            &app.notifications->list()};
}

void lock_paint(LockState &st, LockOutputSurface &los) {
    if (!los.configured || los.egl_surface == EGL_NO_SURFACE || !st.app)
        return;
    using clk = std::chrono::steady_clock;
    clk::time_point t_begin = clk::now();
    clk::time_point t_make, t_wallpaper, t_panel, t_draw, t_swap;

    static int frame = 0;
    int f = ++frame;
    bool trace = f <= 90;
    auto step = [&](const char *what) {
        if (trace)
            klog("lock: paint #%d '%s' %s", f, los.output_name.c_str(), what);
    };

    Renderer &r = st.app->renderer;
    los.canvas.bind(r);
    step("enter -> eglMakeCurrent");
    if (!gl_make_current(st.app->egl_display, los.egl_surface, st.app->egl_context))
        return;
    t_make = clk::now();
    step("begin_frame");
    r.begin_frame(los.width, los.height, los.output_scale.scale);
    glClearColor(astralia::palette::base.r, astralia::palette::base.g, astralia::palette::base.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    auto now = clk::now();
    los.motion.animations.tick(now);
    animated_image_tick(st.avatar, now);

    los.canvas.begin(los.output_scale.scale);
    step("draw_wallpaper");
    if (st.draw_wallpaper)
        st.draw_wallpaper(los.output_name, los.canvas, los.width, los.height);
    t_wallpaper = clk::now();

    step("paint_panel");
    los.hits = {};
    if (!los.panel_gated) {
        float oh = static_cast<float>(los.height);
        astralia::lock_motion_fit(los.motion, lock_card_width(oh), lock_card_height(oh));
        astralia::paint_lock(los.canvas, st.model, los.motion, lock_info(st), static_cast<float>(los.width), oh, [&st, &los](astralia::ui::Canvas &, const astralia::ui::Box &box) {
            if (st.avatar.frames.empty())
                return false;
            animated_image_draw(st.avatar, los.canvas, box.x, box.y, box.w, box.h, 1.0f);
            return true;
        });
        los.hits = st.model.hits();
    }
    los.canvas.flush();
    t_panel = clk::now();
    gl_check("lock_paint");
    t_draw = clk::now();
    step("eglSwapBuffers");
    if (!eglSwapBuffers(st.app->egl_display, los.egl_surface))
        klog("lock: eglSwapBuffers failed on '%s', egl error 0x%04x", los.output_name.c_str(), eglGetError());
    t_swap = clk::now();
    step("swapped");

    auto ms = [](clk::time_point a, clk::time_point b) {
        return std::chrono::duration<float, std::milli>(b - a).count();
    };
    if (ms(t_begin, t_swap) > 50.0f)
        klog("lock: SLOW frame #%d '%s' make=%.1f wallpaper=%.1f panel=%.1f "
             "draw=%.1f swap=%.1f",
             f, los.output_name.c_str(), ms(t_begin, t_make), ms(t_make, t_wallpaper), ms(t_wallpaper, t_panel), ms(t_panel, t_draw), ms(t_draw, t_swap));
    else if (f % 30 == 0)
        klog("lock: paint #%d '%s' locked=%d unlocking=%d gated=%d "
             "scale=%.2f content=%.2f",
             f, los.output_name.c_str(), st.locked, st.unlocking, los.panel_gated, los.motion.panel_scale, los.motion.content_alpha);

    bool avatar_running = st.locked && !st.unlocking && !los.panel_gated && animated_image_animating(st.avatar);
    if (los.motion.animations.hasActive() || avatar_running)
        request_frame(los.frame_clock);
}

void surface_configure(void *data, ext_session_lock_surface_v1 *s, uint32_t serial, uint32_t w, uint32_t h) {
    auto *los = static_cast<LockOutputSurface *>(data);
    ext_session_lock_surface_v1_ack_configure(s, serial);

    LockState *st = los->owner;
    int32_t scale = los->output_scale.scale;
    bool first = los->egl_surface == EGL_NO_SURFACE;
    los->width = static_cast<int32_t>(w);
    los->height = static_cast<int32_t>(h);

    if (first) {
        los->egl_window = wl_egl_window_create(los->surface, los->width * scale, los->height * scale);
        los->egl_surface = eglCreateWindowSurface(st->app->egl_display, st->app->egl_config, reinterpret_cast<EGLNativeWindowType>(los->egl_window), nullptr);
        if (los->egl_surface == EGL_NO_SURFACE) {
            klog("lock: eglCreateWindowSurface failed on '%s'", los->output_name.c_str());
            return;
        }
        los->frame_clock.surface = los->surface;
        los->frame_clock.draw = [st, los] { lock_paint(*st, *los); };
    } else if (los->egl_window) {
        wl_egl_window_resize(los->egl_window, los->width * scale, los->height * scale, 0, 0);
    }
    los->configured = true;
    request_frame(los->frame_clock);
    app_detail::rest_egl_current(*st->app);
}

constexpr ext_session_lock_surface_v1_listener kSurfaceListener = {
    .configure = surface_configure,
};

void handle_locked(void *data, ext_session_lock_v1 *) {
    auto *st = static_cast<LockState *>(data);
    if (!st->active)
        return;
    st->locked = true;
    st->locked_at = std::chrono::steady_clock::now();
    if (st->app)
        st->app->session_locked = true;
    klog("lock: session locked, %zu surface(s)", st->surfaces.size());
    for (auto &up : st->surfaces)
        klog("lock:   '%s' configured=%d egl=%d %dx%d", up->output_name.c_str(), up->configured, up->egl_surface != EGL_NO_SURFACE, up->width, up->height);
    request_all(*st);
}

void handle_finished(void *data, ext_session_lock_v1 *) {
    auto *st = static_cast<LockState *>(data);
    if (!st->lock)
        return;
    klog("lock: compositor sent finished");
    if (st->locked)
        ext_session_lock_v1_unlock_and_destroy(st->lock);
    else
        ext_session_lock_v1_destroy(st->lock);
    st->lock = nullptr;
    st->locked = false;
    lock_teardown(*st);
}

constexpr ext_session_lock_v1_listener kLockListener = {
    .locked = handle_locked,
    .finished = handle_finished,
};

void deliver_auth(LockState &st, uint64_t gen, pam_auth::Result res) {
    if (!st.locked)
        return;
    bool current = gen == st.model.generation();
    if (st.model.finish_auth(gen, res.success)) {
        lock_begin_unlock(st);
        return;
    }
    if (!current)
        return;
    for (auto &up : st.surfaces)
        astralia::lock_motion_clear_dots(up->motion);
    request_all(st);
}

void try_authenticate(LockState &st) {
    if (st.model.authenticating() || st.model.password().empty())
        return;
    std::string pw = st.model.begin_auth();
    uint64_t gen = st.model.generation();
    std::thread([&st, gen, pw = std::move(pw)]() mutable {
        pam_auth::Result res = pam_auth::authenticate(user_info::username(), pw, ASTRALIA_SHELL_PAM_DIR);
        pam_auth::secure_clear(pw);
        astralia::DeferredCall::call_later([&st, gen, res] { deliver_auth(st, gen, res); });
    }).detach();
    request_all(st);
}

void create_output_surface(LockState &st, wl_output *output, const std::string &name) {
    auto los = std::make_unique<LockOutputSurface>();
    los->owner = &st;
    los->output = output;
    los->output_name = name;
    los->surface = wl_compositor_create_surface(st.app->compositor);
    los->lock_surface =
        ext_session_lock_v1_get_lock_surface(st.lock, los->surface, output);
    if (!los->lock_surface) {
        klog("lock: get_lock_surface failed on '%s'", name.c_str());
        wl_surface_destroy(los->surface);
        return;
    }
    ext_session_lock_surface_v1_add_listener(los->lock_surface, &kSurfaceListener, los.get());
    los->output_scale.on_change = [ptr = los.get()](int32_t s) {
        if (ptr->egl_window)
            wl_egl_window_resize(ptr->egl_window, ptr->width * s, ptr->height * s, 0, 0);
        if (ptr->frame_clock.surface)
            request_frame(ptr->frame_clock);
    };
    output_scale_watch(los->output_scale, los->surface);
    los->panel_gated = st.panel_gated_for && !st.panel_gated_for(name);
    st.surfaces.push_back(std::move(los));
}

void destroy_output_surface(LockState &st, LockOutputSurface &los) {
    if (los.frame_clock.callback) {
        wl_callback_destroy(los.frame_clock.callback);
        los.frame_clock.callback = nullptr;
    }
    if (los.egl_surface != EGL_NO_SURFACE) {
        gl_release_if_current(st.app->egl_display, los.egl_surface);
        eglDestroySurface(st.app->egl_display, los.egl_surface);
        los.egl_surface = EGL_NO_SURFACE;
    }
    if (los.egl_window) {
        wl_egl_window_destroy(los.egl_window);
        los.egl_window = nullptr;
    }
    if (los.lock_surface) {
        ext_session_lock_surface_v1_destroy(los.lock_surface);
        los.lock_surface = nullptr;
    }
    if (los.surface) {
        wl_surface_destroy(los.surface);
        los.surface = nullptr;
    }
}

void finish_unlock(LockState &st) {
    if (!st.lock)
        return;
    ext_session_lock_v1_unlock_and_destroy(st.lock);
    st.lock = nullptr;
    wl_display_roundtrip(st.app->display);
    lock_teardown(st);
    klog("lock: session unlocked");
}

} // namespace

bool lock_request(LockState &st, WaylandState &app) {
    if (st.active)
        return true;
    if (!app.session_lock_manager) {
        klog("lock: compositor has no ext_session_lock_manager_v1");
        return false;
    }
    st.app = &app;
    st.lock = ext_session_lock_manager_v1_lock(app.session_lock_manager);
    if (!st.lock) {
        klog("lock: failed to create session lock");
        return false;
    }
    ext_session_lock_v1_add_listener(st.lock, &kLockListener, &st);

    st.active = true;
    st.locked = false;
    st.unlocking = false;
    st.model.reset();
    if (st.user.empty())
        st.user = user_info::username();

    AnimatedImageStyle avatar_style;
    avatar_style.size = kLockProfileSize;
    avatar_style.circular = true;
    avatar_style.border_width = kLockProfileBorderWidth;
    avatar_style.decode = {static_cast<int>(kLockAvatarFps), static_cast<int>(kLockProfileSize) * 2};
    animated_image_set_source(st.avatar, user_info::profile_media_path(), avatar_style);
    animated_image_show(st.avatar, [&st] { request_all(st); });

    for (auto &mon : app.outputs)
        create_output_surface(st, mon->output.wl, mon->output.name);

    wl_display_flush(app.display);
    klog("lock: requested (%zu surface(s))", st.surfaces.size());
    return true;
}

void lock_teardown(LockState &st) {
    for (auto &up : st.surfaces)
        destroy_output_surface(st, *up);
    st.surfaces.clear();
    animated_image_hide(st.avatar);
    st.active = false;
    st.locked = false;
    st.unlocking = false;
    if (st.app)
        st.app->session_locked = false;
    st.model.reset();
    if (st.app) {
        app_detail::rest_egl_current(*st.app);
        for (auto &mon : st.app->outputs)
            request_all_frames(*mon);
        wl_display_flush(st.app->display);
    }
}

void lock_begin_unlock(LockState &st) {
    if (st.unlocking || !st.locked)
        return;
    st.unlocking = true;
    for (auto &up : st.surfaces) {
        bool primary = up.get() == st.surfaces.front().get();
        astralia::lock_motion_exit(up->motion, [&st, primary] {
            if (primary)
                astralia::DeferredCall::call_later([&st] { finish_unlock(st); });
        });
        if (up->frame_clock.surface)
            request_frame(up->frame_clock);
    }
    if (st.app)
        app_detail::rest_egl_current(*st.app);
}

void lock_handle_key(LockState &st, const KeyEvent &ev) {
    if (!st.locked || st.unlocking)
        return;
    switch (st.model.key(to_neutral(ev))) {
    case astralia::LockKey::submit:
        try_authenticate(st);
        return;
    case astralia::LockKey::changed:
        for (auto &up : st.surfaces)
            astralia::lock_motion_type(up->motion, st.model.password());
        request_all(st);
        return;
    case astralia::LockKey::none:
        return;
    }
}

void lock_handle_click(LockState &st, wl_surface *surf, double x, double y) {
    LockOutputSurface *los = surface_for(st, surf);
    if (!los)
        return;
    if (x <= 140.0 && y >= static_cast<double>(los->height) - 48.0) {
        lock_begin_unlock(st);
        return;
    }
    if (st.unlocking || !st.app)
        return;

    auto hit = [x, y](const astralia::ui::Box &r) {
        return r.w > 0.0f && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
    };
    if (hit(los->hits.pill_button)) {
        try_authenticate(st);
        return;
    }
    if (hit(los->hits.media_prev)) {
        st.app->media->previous();
        request_all(st);
    } else if (hit(los->hits.media_play)) {
        st.app->media->play_pause();
        request_all(st);
    } else if (hit(los->hits.media_next)) {
        st.app->media->next();
        request_all(st);
    }
}

void lock_timer_tick(LockState &st) {
    if (!st.active)
        return;
    st.model.tick(astralia::LockModel::Clock::now());
    request_all(st);
}

void lock_hotplug_add(LockState &st, wl_output *output, const char *name) {
    if (!st.active)
        return;
    for (auto &up : st.surfaces)
        if (up->output == output)
            return;
    create_output_surface(st, output, name ? name : "");
    wl_display_flush(st.app->display);
}

void lock_hotplug_remove(LockState &st, wl_output *output) {
    if (!st.active)
        return;
    auto it =
        std::find_if(st.surfaces.begin(), st.surfaces.end(), [output](const std::unique_ptr<LockOutputSurface> &u) { return u->output == output; });
    if (it == st.surfaces.end())
        return;
    destroy_output_surface(st, **it);
    st.surfaces.erase(it);
}

wl_surface *lock_focused_surface(const LockState &st) {
    if (st.surfaces.empty())
        return nullptr;
    if (st.app && st.app->keyboard.focused_surface) {
        for (auto &up : st.surfaces)
            if (up->surface == st.app->keyboard.focused_surface)
                return up->surface;
    }
    return st.surfaces.front()->surface;
}

bool lock_owns_surface(const LockState &st, wl_surface *s) {
    if (!s)
        return false;
    for (auto &up : st.surfaces)
        if (up->surface == s)
            return true;
    return false;
}

namespace {

class LockModule final : public Module {
  public:
    explicit LockModule(LockWallpaperDrawFn draw_wallpaper) : draw_wallpaper_(std::move(draw_wallpaper)) {}

    LockState &state() { return state_; }

    const char *name() const override { return "lock"; }
    bool is_open() const override { return state_.active; }

    bool create_surface(WaylandState &, wl_output *) override { return true; }

    bool init_egl(WaylandState &app) override {
        state_.app = &app;
        state_.draw_wallpaper = [this, &app](const std::string &output_name, GlCanvas &canvas, int32_t w, int32_t h) {
            if (draw_wallpaper_)
                draw_wallpaper_(app, output_name, canvas, w, h);
        };
        state_.panel_gated_for = [&app](const std::string &output_name) {
            return lock_effective_enabled(app.cfg, output_name);
        };
        return true;
    }

    bool configured() const override { return true; }
    wl_surface *surface() const override {
        return lock_focused_surface(state_);
    }
    bool owns_surface(wl_surface *s) const override {
        return lock_owns_surface(state_, s);
    }
    void request_frame() override {}

    bool timer_tick(WaylandState &app) override {
        if (!state_.active)
            return false;
        ++poll_tick_;
        astralia::cpu_temp_poll(app.cpu_temp);
        astralia::system_stats_poll(app.system_stats);
        if (poll_tick_ % 5 == 0 || (app.gpu_temp.nvidia_smi_proc && app.gpu_temp.nvidia_smi_proc->running()))
            astralia::gpu_temp_poll(app.gpu_temp);
        lock_timer_tick(state_);
        return true;
    }

    void handle_key_event(WaylandState &, const KeyEvent &event) override {
        lock_handle_key(state_, event);
    }
    void handle_click(WaylandState &app, double x, double y) override {
        lock_handle_click(state_, app.pointer.focused_surface, x, y);
    }

    std::vector<astralia::ShellBinding> shell_bindings(WaylandState &app) override {
        return {{astralia::ShellVerb::lock,
                 [this, &app] {
                     astralia::cpu_temp_poll(app.cpu_temp);
                     astralia::system_stats_poll(app.system_stats);
                     astralia::gpu_temp_poll(app.gpu_temp);
                     lock_request(state_, app);
                 }}};
    }

  private:
    LockWallpaperDrawFn draw_wallpaper_;
    LockState state_;
    int poll_tick_ = 0;
};

LockModule *find_lock_module(WaylandState &app) {
    for (auto &m : app.overlays)
        if (auto *lm = dynamic_cast<LockModule *>(m.get()))
            return lm;
    return nullptr;
}

} // namespace

std::unique_ptr<Module> make_lock_module(LockWallpaperDrawFn draw_wallpaper) {
    return std::make_unique<LockModule>(std::move(draw_wallpaper));
}

void lock_notify_output_added(WaylandState &app, wl_output *output, const char *name) {
    if (auto *lm = find_lock_module(app))
        lock_hotplug_add(lm->state(), output, name);
}

void lock_notify_output_removed(WaylandState &app, wl_output *output) {
    if (auto *lm = find_lock_module(app))
        lock_hotplug_remove(lm->state(), output);
}

void lock_start(WaylandState &app) {
    if (auto *lm = find_lock_module(app))
        lock_request(lm->state(), app);
}
