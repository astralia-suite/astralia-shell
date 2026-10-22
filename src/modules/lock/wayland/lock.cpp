#include <GLES3/gl32.h>
#include <cctype>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

#include "wayland/app/monitor_output.h"
#include "wayland/app/user_info.h"
#include "wayland/app/wayland_state.h"
#include "wayland/core/log.h"
#include "wayland/render/gl.h"

#include "app/session.h"

#include "core/deferred_call.h"

#include "modules/lock/layout.h"
#include "modules/lock/pam.h"
#include "modules/lock/view.h"
#include "modules/lock/wayland/lock.h"

#include "render/tokens.h"

#include "service/telemetry_service.h"

#ifndef ASTRALIA_SHELL_PAM_DIR
#define ASTRALIA_SHELL_PAM_DIR ""
#endif

namespace astralia::lock {

namespace {

void render(State &st, OutputSurface &los);
void finish_unlock(State &st);

void load_wallpaper(State &st, WaylandState &app) {
    if (app.cfg.lock_wallpaper_custom && !st.wallpaper.loaded())
        st.wallpaper.load(app);
}

void release_wallpaper_if_idle(State &st) {
    if (!st.active && !st.transition.active)
        st.wallpaper.release();
}

void stop_transition(State &st) {
    transition_stop(st.transition);
    release_wallpaper_if_idle(st);
}

OutputSurface *surface_for(State &st, wl_surface *s) {
    for (auto &up : st.surfaces)
        if (up->surface == s)
            return up.get();
    return nullptr;
}

void request_all(State &st) {
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

Info info(const State &st) {
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
            user_info::face_path(),
            &app.battery->status(),
            &app.media->status(),
            &app.system_stats,
            &app.cpu_temp,
            &app.gpu_temp,
            &app.notifications->list()};
}

void render(State &st, OutputSurface &los) {
    if (!los.configured || los.egl_surface == EGL_NO_SURFACE || !st.app)
        return;
    using clk = std::chrono::steady_clock;
    clk::time_point t_begin = clk::now();
    clk::time_point t_make, t_wallpaper, t_panel, t_draw, t_swap;

    static int frame = 0;
    int f = ++frame;

    Renderer &r = st.app->renderer;
    los.canvas.bind(r);
    if (!gl_make_current(st.app->egl_display, los.egl_surface, st.app->egl_context))
        return;
    t_make = clk::now();
    r.begin_frame(los.width, los.height, los.output_scale.scale);
    glClearColor(astralia::palette::base.r, astralia::palette::base.g, astralia::palette::base.b,
                 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    auto now = clk::now();
    los.motion.animations.tick(now);

    los.canvas.begin(los.output_scale.scale);
    if (st.draw_wallpaper)
        st.draw_wallpaper(los.output_name, los.canvas, los.width, los.height);
    t_wallpaper = clk::now();

    los.hits = {};
    if (!los.panel_gated) {
        float oh = static_cast<float>(los.height);
        motion_fit(los.motion, card_width(oh), card_height(oh));
        lock::paint(los.canvas, st.model, los.motion, info(st), static_cast<float>(los.width), oh);
        los.hits = st.model.hits();
    }
    los.canvas.flush();
    t_panel = clk::now();
    gl_check("lock_paint");
    t_draw = clk::now();
    if (!eglSwapBuffers(st.app->egl_display, los.egl_surface))
        klog("lock: eglSwapBuffers failed on '%s', egl error 0x%04x", los.output_name.c_str(),
             eglGetError());
    t_swap = clk::now();

    auto ms = [](clk::time_point a, clk::time_point b) {
        return std::chrono::duration<float, std::milli>(b - a).count();
    };
    if (ms(t_begin, t_swap) > 50.0f)
        klog("lock: SLOW frame #%d '%s' make=%.1f wallpaper=%.1f panel=%.1f "
             "draw=%.1f swap=%.1f",
             f, los.output_name.c_str(), ms(t_begin, t_make), ms(t_make, t_wallpaper),
             ms(t_wallpaper, t_panel), ms(t_panel, t_draw), ms(t_draw, t_swap));

    if (los.motion.animations.has_active())
        request_frame(los.frame_clock);
}

void surface_configure(void *data, ext_session_lock_surface_v1 *s, uint32_t serial, uint32_t w,
                       uint32_t h) {
    auto *los = static_cast<OutputSurface *>(data);
    ext_session_lock_surface_v1_ack_configure(s, serial);

    State *st = los->owner;
    int32_t scale = los->output_scale.scale;
    bool first = los->egl_surface == EGL_NO_SURFACE;
    los->width = static_cast<int32_t>(w);
    los->height = static_cast<int32_t>(h);

    if (first) {
        los->egl_window =
            wl_egl_window_create(los->surface, los->width * scale, los->height * scale);
        los->egl_surface =
            eglCreateWindowSurface(st->app->egl_display, st->app->egl_config,
                                   reinterpret_cast<EGLNativeWindowType>(los->egl_window), nullptr);
        if (los->egl_surface == EGL_NO_SURFACE) {
            klog("lock: eglCreateWindowSurface failed on '%s'", los->output_name.c_str());
            return;
        }
        los->frame_clock.surface = los->surface;
        los->frame_clock.draw = [st, los] { render(*st, *los); };
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
    auto *st = static_cast<State *>(data);
    if (!st->active)
        return;
    if (st->acknowledge_startup) {
        st->acknowledge_startup = false;
        astralia::start_locked_ack();
    }
    st->locked = true;
    st->locked_at = std::chrono::steady_clock::now();
    if (st->app)
        st->app->session_locked = true;
    klog("lock: session locked, %zu surface(s)", st->surfaces.size());
    for (auto &up : st->surfaces)
        klog("lock:   '%s' configured=%d egl=%d %dx%d", up->output_name.c_str(), up->configured,
             up->egl_surface != EGL_NO_SURFACE, up->width, up->height);
    request_all(*st);
    transition_release_keyboard(st->transition);
}

void handle_finished(void *data, ext_session_lock_v1 *) {
    auto *st = static_cast<State *>(data);
    if (!st->lock)
        return;
    klog("lock: compositor sent finished");
    if (st->locked)
        ext_session_lock_v1_unlock_and_destroy(st->lock);
    else
        ext_session_lock_v1_destroy(st->lock);
    st->lock = nullptr;
    st->locked = false;
    teardown(*st);
    stop_transition(*st);
}

constexpr ext_session_lock_v1_listener kListener = {
    .locked = handle_locked,
    .finished = handle_finished,
};

void deliver_auth(State &st, uint64_t gen, pam::Result res) {
    klog("lock: auth result success=%d locked=%d", res.success, st.locked);
    if (!st.locked)
        return;
    bool current = gen == st.model.generation();
    if (st.model.finish_auth(gen, res.success)) {
        begin_unlock(st);
        return;
    }
    if (!current)
        return;
    for (auto &up : st.surfaces)
        motion_clear_dots(up->motion);
    request_all(st);
}

void try_authenticate(State &st) {
    if (st.model.authenticating() || st.model.password().empty()) {
        klog("lock: submit ignored (authenticating=%d empty=%d)", st.model.authenticating(),
             st.model.password().empty());
        return;
    }
    klog("lock: auth started");
    std::string pw = st.model.begin_auth();
    uint64_t gen = st.model.generation();
    std::thread([&st, gen, pw = std::move(pw)]() mutable {
        pam::Result res = pam::authenticate(user_info::username(), pw, ASTRALIA_SHELL_PAM_DIR);
        pam::secure_clear(pw);
        astralia::DeferredCall::call_later([&st, gen, res] { deliver_auth(st, gen, res); });
    }).detach();
    request_all(st);
}

void create_output_surface(State &st, wl_output *output, const std::string &name) {
    auto los = std::make_unique<OutputSurface>();
    los->owner = &st;
    los->output = output;
    los->output_name = name;
    los->surface = wl_compositor_create_surface(st.app->compositor);
    los->lock_surface = ext_session_lock_v1_get_lock_surface(st.lock, los->surface, output);
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

void destroy_output_surface(State &st, OutputSurface &los) {
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

void release_session(State &st) {
    ext_session_lock_v1_unlock_and_destroy(st.lock);
    st.lock = nullptr;
    wl_display_roundtrip(st.app->display);
    teardown(st);
    klog("lock: session unlocked");
}

void finish_unlock(State &st) {
    if (!st.lock)
        return;
    release_session(st);
    stop_transition(st);
}

} // namespace

bool request(State &st, WaylandState &app) {
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
    ext_session_lock_v1_add_listener(st.lock, &kListener, &st);

    st.active = true;
    st.locked = false;
    st.unlocking = false;
    st.model.reset();
    load_wallpaper(st, app);
    if (st.user.empty())
        st.user = user_info::username();

    for (auto &mon : app.outputs)
        create_output_surface(st, mon->output.wl, mon->output.name);

    wl_display_flush(app.display);
    klog("lock: requested (%zu surface(s))", st.surfaces.size());
    return true;
}

void lock_begin(State &st, WaylandState &app, bool animate) {
    if (st.active || st.transition.active)
        return;
    st.app = &app;
    load_wallpaper(st, app);
    bool animated = animate && !app.cfg.animations_disabled && app.session_lock_manager &&
                    !app.outputs.empty() && transition_open(st.transition, app);
    if (!animated) {
        if (!request(st, app))
            release_wallpaper_if_idle(st);
        return;
    }
    transition_play(st.transition, [&st, &app] {
        if (!request(st, app))
            stop_transition(st);
    });
}

void teardown(State &st) {
    for (auto &up : st.surfaces)
        destroy_output_surface(st, *up);
    st.surfaces.clear();
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
    release_wallpaper_if_idle(st);
}

void begin_unlock(State &st) {
    if (st.unlocking || !st.locked)
        return;
    st.unlocking = true;
    bool reuse = st.transition.active && !st.transition.playing && !st.transition.surfaces.empty();
    bool animated = st.app && !st.app->cfg.animations_disabled && !st.surfaces.empty() &&
                    (reuse || transition_open(st.transition, *st.app, TransitionMode::unlock));
    klog("lock: unlock begin (reuse=%d animated=%d)", reuse, animated);
    if (!animated) {
        astralia::DeferredCall::call_later([&st] { finish_unlock(st); });
        return;
    }
    st.transition.on_ready = [&st](bool held) {
        klog("lock: unlock overlays ready=%d", held);
        if (!st.lock)
            return;
        if (!held) {
            finish_unlock(st);
            return;
        }
        release_session(st);
        if (!transition_play(st.transition, [&st] { stop_transition(st); }))
            stop_transition(st);
    };
    if (reuse)
        transition_rehold(st.transition, TransitionMode::unlock);
    app_detail::rest_egl_current(*st.app);
}

void handle_key(State &st, const KeyEvent &ev) {
    if (!st.locked || st.unlocking)
        return;
    switch (st.model.key(to_neutral(ev))) {
    case Key::submit:
        try_authenticate(st);
        return;
    case Key::changed:
        for (auto &up : st.surfaces)
            motion_type(up->motion, st.model.password());
        request_all(st);
        return;
    case Key::none:
        return;
    }
}

void handle_click(State &st, wl_surface *surf, double x, double y) {
    OutputSurface *los = surface_for(st, surf);
    if (!los)
        return;
    if (dev_unlock_hit(x, y, static_cast<double>(los->height))) {
        begin_unlock(st);
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

void timer_tick(State &st) {
    if (!st.active)
        return;
    st.model.tick(Model::Clock::now());
    request_all(st);
}

void hotplug_add(State &st, wl_output *output, const char *name) {
    if (!st.active)
        return;
    for (auto &up : st.surfaces)
        if (up->output == output)
            return;
    create_output_surface(st, output, name ? name : "");
    wl_display_flush(st.app->display);
}

void hotplug_remove(State &st, wl_output *output) {
    if (!st.active)
        return;
    auto it = std::find_if(
        st.surfaces.begin(), st.surfaces.end(),
        [output](const std::unique_ptr<OutputSurface> &u) { return u->output == output; });
    if (it == st.surfaces.end())
        return;
    destroy_output_surface(st, **it);
    st.surfaces.erase(it);
}

wl_surface *focused_surface(const State &st) {
    if (st.surfaces.empty())
        return nullptr;
    if (st.app && st.app->keyboard.focused_surface) {
        for (auto &up : st.surfaces)
            if (up->surface == st.app->keyboard.focused_surface)
                return up->surface;
    }
    return st.surfaces.front()->surface;
}

bool owns_surface(const State &st, wl_surface *s) {
    if (!s)
        return false;
    for (auto &up : st.surfaces)
        if (up->surface == s)
            return true;
    return false;
}

namespace {

class Adapter final : public Module {
  public:
    explicit Adapter(WallpaperDrawFn draw_wallpaper) : draw_wallpaper_(std::move(draw_wallpaper)) {}

    State &state() { return state_; }

    const char *name() const override { return "lock"; }
    bool is_open() const override { return state_.active; }

    bool create_surface(WaylandState &, wl_output *) override { return true; }

    bool init_egl(WaylandState &app) override {
        state_.app = &app;
        state_.wallpaper.request_frames = [this] { request_all(state_); };
        state_.draw_wallpaper = [this, &app](const std::string &output_name, GlCanvas &canvas,
                                             int32_t w, int32_t h) {
            if (state_.wallpaper.loaded())
                state_.wallpaper.draw(output_name, canvas, w, h);
            else if (draw_wallpaper_)
                draw_wallpaper_(app, output_name, canvas, w, h);
        };
        state_.transition.draw_wallpaper = state_.draw_wallpaper;
        state_.transition.wallpaper_ready = [this](const std::string &output_name) {
            return state_.wallpaper.ready(output_name);
        };
        state_.transition.draw_lock = [this](const std::string &output_name, GlCanvas &canvas,
                                             int32_t w, int32_t h, Motion &motion) {
            state_.draw_wallpaper(output_name, canvas, w, h);
            if (state_.panel_gated_for && !state_.panel_gated_for(output_name))
                return;
            if (state_.user.empty())
                state_.user = user_info::username();
            Motion *shown = &motion;
            if (state_.unlocking)
                for (auto &up : state_.surfaces)
                    if (up->output_name == output_name)
                        shown = &up->motion;
            float oh = static_cast<float>(h);
            motion_fit(*shown, card_width(oh), card_height(oh));
            lock::paint(canvas, state_.model, *shown, info(state_), static_cast<float>(w), oh);
        };
        state_.panel_gated_for = [&app](const std::string &output_name) {
            return lock_enabled_for(app.cfg, output_name);
        };
        return true;
    }

    bool configured() const override { return true; }
    wl_surface *surface() const override { return lock::focused_surface(state_); }
    bool owns_surface(wl_surface *s) const override { return lock::owns_surface(state_, s); }
    void request_frame() override {}

    bool timer_tick(WaylandState &app) override {
        transition_check_deadline(state_.transition);
        if (!state_.active)
            return false;
        ++poll_tick_;
        astralia::cpu_temp_poll(app.cpu_temp);
        astralia::system_stats_poll(app.system_stats);
        if (poll_tick_ % 5 == 0 ||
            (app.gpu_temp.nvidia_smi_proc && app.gpu_temp.nvidia_smi_proc->running()))
            astralia::gpu_temp_poll(app.gpu_temp);
        lock::timer_tick(state_);
        return true;
    }

    void handle_key_event(WaylandState &, const KeyEvent &event) override {
        handle_key(state_, event);
    }
    void handle_click(WaylandState &app, double x, double y) override {
        lock::handle_click(state_, app.pointer.focused_surface, x, y);
    }

    std::vector<astralia::ShellBinding> shell_bindings(WaylandState &app) override {
        return {{astralia::ShellVerb::lock, [this, &app] {
                     astralia::cpu_temp_poll(app.cpu_temp);
                     astralia::system_stats_poll(app.system_stats);
                     astralia::gpu_temp_poll(app.gpu_temp);
                     lock_begin(state_, app);
                 }}};
    }

  private:
    WallpaperDrawFn draw_wallpaper_;
    State state_;
    int poll_tick_ = 0;
};

Adapter *find_lock_module(WaylandState &app) {
    for (auto &m : app.overlays)
        if (auto *lm = dynamic_cast<Adapter *>(m.get()))
            return lm;
    return nullptr;
}

} // namespace

std::unique_ptr<Module> make_module(WallpaperDrawFn draw_wallpaper) {
    return std::make_unique<Adapter>(std::move(draw_wallpaper));
}

void at_startup(WaylandState &app) {
    auto *lm = find_lock_module(app);
    if (!lm) {
        klog("lock: start locked requested but the lock module is not loaded");
        return;
    }
    astralia::cpu_temp_poll(app.cpu_temp);
    astralia::system_stats_poll(app.system_stats);
    astralia::gpu_temp_poll(app.gpu_temp);
    lm->state().acknowledge_startup = true;
    lock_begin(lm->state(), app, false);
}

void notify_output_added(WaylandState &app, wl_output *output, const char *name) {
    if (auto *lm = find_lock_module(app))
        hotplug_add(lm->state(), output, name);
}

void notify_output_removed(WaylandState &app, wl_output *output) {
    if (auto *lm = find_lock_module(app)) {
        transition_output_removed(lm->state().transition, output);
        hotplug_remove(lm->state(), output);
    }
}

} // namespace astralia::lock
