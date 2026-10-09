#include <GLES3/gl32.h>
#include <string>
#include <thread>
#include <vector>

#include "wayland/app/monitor_output.h"
#include "wayland/app/user_info.h"
#include "wayland/app/wayland_state.h"

#include "core/deferred_call.h"
#include "wayland/core/log.h"

#include "modules/lock/wayland/card.h"
#include "modules/lock/wayland/layout.h"
#include "modules/lock/wayland/lock.h"
#include "modules/lock/wayland/pam_authenticator.h"

#include "ui/tokens.h"
#include "wayland/render/gl.h"
#include "wayland/render/image.h"
#include "wayland/render/node.h"

#include "service/telemetry_service.h"

namespace {

void lock_paint(LockState &st, LockOutputSurface &los);
void start_init_anim(LockState &st, LockOutputSurface &los);
void start_unlock_anim(LockState &st, LockOutputSurface &los);
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

void start_init_anim(LockState &st, LockOutputSurface &los) {
    (void)st;
    klog("lock: init anim start on '%s' box=%.0f target=%.0fx%.0f", los.output_name.c_str(), lock_icon_box_size(), los.panel_w_target, los.panel_h_target);
    los.anim_started = true;
    los.panel_scale = kLockScaleHidden;
    los.panel_rotation = 0.0f;
    los.icon_alpha = 1.0f;
    los.content_alpha = 0.0f;
    los.content_scale = kLockScaleHidden;

    float box = lock_icon_box_size();
    los.panel_w = box;
    los.panel_h = box;
    float target_w = los.panel_w_target > 0 ? los.panel_w_target : box;
    float target_h = los.panel_h_target > 0 ? los.panel_h_target : box;

    auto &a = los.animations;
    a.animate(kLockScaleHidden, kLockScaleFull, kLockAnimSpinMs, astralia::Easing::EaseOutBack, [&los](float v) { los.panel_scale = v; }, {}, kLockOwnerPanelScale);
    a.animate(0.0f, 360.0f, kLockAnimSpinMs, astralia::Easing::EaseInOutCubic, [&los](float v) { los.panel_rotation = v; }, [&los, target_w, target_h] {
            klog("lock: entrance expand begin on '%s' -> %.0fx%.0f", los.output_name.c_str(), target_w, target_h);
            los.panel_rotation = 0.0f;
            auto &a2 = los.animations;
            a2.animate(los.panel_w, target_w, kLockAnimExpandMs, astralia::Easing::EaseOutCubic, [&los](float v) { los.panel_w = v; }, {}, kLockOwnerPanelWidth);
            a2.animate(los.panel_h, target_h, kLockAnimExpandMs, astralia::Easing::EaseOutCubic, [&los](float v) { los.panel_h = v; }, {}, kLockOwnerPanelHeight);
            a2.animate(1.0f, 0.0f, kLockAnimIconFadeOutMs, astralia::Easing::EaseOutCubic, [&los](float v) { los.icon_alpha = v; }, {}, kLockOwnerIconAlpha);
            a2.animate(0.0f, 1.0f, kLockAnimContentFadeInMs, astralia::Easing::EaseOutCubic, [&los](float v) { los.content_alpha = v; }, {}, kLockOwnerContentAlpha);
            a2.animate(kLockScaleHidden, kLockScaleFull, kLockAnimContentScaleInMs, astralia::Easing::EaseOutBack, [&los](float v) { los.content_scale = v; }, {}, kLockOwnerContentScale); }, kLockOwnerPanelRotation);
}

void sync_panel_size(LockState &st, LockOutputSurface &los) {
    float oh = static_cast<float>(los.height);
    float card_w = lock_card_width(oh);
    float card_h = lock_card_height(oh);
    los.panel_w_target = card_w;
    los.panel_h_target = card_h;

    if (!los.anim_started)
        start_init_anim(st, los);
    else if (!los.animations.hasActive() && !st.unlocking) {
        los.panel_w = card_w;
        los.panel_h = card_h;
    }
}

void start_unlock_anim(LockState &st, LockOutputSurface &los) {
    float box = lock_icon_box_size();
    auto &a = los.animations;
    a.animate(los.panel_w, box, kLockAnimShrinkMs, astralia::Easing::EaseInCubic, [&los](float v) { los.panel_w = v; }, {}, kLockOwnerPanelWidth);
    a.animate(los.panel_h, box, kLockAnimShrinkMs, astralia::Easing::EaseInCubic, [&los](float v) { los.panel_h = v; }, {}, kLockOwnerPanelHeight);
    a.animate(los.icon_alpha, 1.0f, kLockAnimIconFadeInMs, astralia::Easing::EaseInCubic, [&los](float v) { los.icon_alpha = v; }, {}, kLockOwnerIconAlpha);
    a.animate(los.content_alpha, 0.0f, kLockAnimContentFadeOutMs, astralia::Easing::EaseInCubic, [&los](float v) { los.content_alpha = v; }, {}, kLockOwnerContentAlpha);
    a.animate(los.content_scale, kLockScaleHidden, kLockAnimContentScaleOutMs, astralia::Easing::EaseInBack, [&los](float v) { los.content_scale = v; }, {}, kLockOwnerContentScale);

    bool is_primary = !st.surfaces.empty() && st.surfaces.front().get() == &los;
    a.animate(0.0f, 1.0f, kLockAnimShrinkMs, astralia::Easing::Linear, [](float) {}, [&st, &los, is_primary] {
            auto &a2 = los.animations;
            a2.animate(los.panel_scale, kLockScaleHidden, kLockAnimSpinMs, astralia::Easing::EaseInBack, [&los](float v) { los.panel_scale = v; }, {}, kLockOwnerPanelScale);
            a2.animate(0.0f, -360.0f, kLockAnimSpinMs, astralia::Easing::EaseInOutCubic, [&los](float v) { los.panel_rotation = v; }, [&st, is_primary] {
                    if (is_primary)
                        astralia::DeferredCall::call_later([&st] { finish_unlock(st); });
                }, kLockOwnerPanelRotation); }, kLockOwnerSequence);
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
    step("enter -> eglMakeCurrent");
    if (!gl_make_current(st.app->egl_display, los.egl_surface, st.app->egl_context))
        return;
    t_make = clk::now();
    step("begin_frame");
    r.begin_frame(los.width, los.height, los.output_scale.scale);
    glClearColor(astralia::palette::base.r, astralia::palette::base.g, astralia::palette::base.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    auto now = clk::now();
    los.animations.tick(now);
    animated_image_tick(st.avatar, now);

    los.scene.rebuild();
    Node &root = los.scene.root;

    step("draw_wallpaper");
    if (st.draw_wallpaper)
        st.draw_wallpaper(los.output_name, root, los.width, los.height);
    t_wallpaper = clk::now();

    step("build_panel");
    if (!los.panel_gated) {
        sync_panel_size(st, los);
        lock_card_build(st, los, &root);
    }
    t_panel = clk::now();

    step("scene.draw");
    r.set_opacity(1.0f);
    los.scene.draw(r);
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
             f, los.output_name.c_str(), st.locked, st.unlocking, los.panel_gated, los.panel_scale, los.content_alpha);

    bool avatar_running = st.locked && !st.unlocking && !los.panel_gated && animated_image_animating(st.avatar);
    if (los.animations.hasActive() || avatar_running)
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
    if (gen != st.auth_generation || !st.locked)
        return;
    st.authenticating = false;
    if (res.success) {
        lock_begin_unlock(st);
        return;
    }
    pam_auth::secure_clear(st.password.text);
    st.pw_anim.chars.clear();
    st.pw_row_slide = {};
    st.failed = true;
    st.fail_clear_at =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(static_cast<int>(kLockTimerFailMs));
    request_all(st);
}

void try_authenticate(LockState &st) {
    if (st.authenticating || st.password.text.empty())
        return;
    st.authenticating = true;
    st.failed = false;
    uint64_t gen = ++st.auth_generation;
    std::string pw = st.password.text;
    std::thread([&st, gen, pw = std::move(pw)]() mutable {
        pam_auth::Result res = pam_auth::authenticate_current_user(pw);
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
    st.failed = false;
    st.authenticating = false;
    st.password.text.clear();
    st.pw_anim.chars.clear();
    st.pw_row_slide = {};
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
    pam_auth::secure_clear(st.password.text);
    st.pw_anim.chars.clear();
    st.pw_row_slide = {};
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
        start_unlock_anim(st, *up);
        if (up->frame_clock.surface)
            request_frame(up->frame_clock);
    }
    if (st.app)
        app_detail::rest_egl_current(*st.app);
}

void lock_handle_key(LockState &st, const KeyEvent &ev) {
    if (!st.locked || st.unlocking)
        return;
    TextFieldResult res = text_field_handle_key(st.password, ev);
    if (res == TextFieldResult::Committed) {
        try_authenticate(st);
        return;
    }
    if (res == TextFieldResult::Cancelled)
        pam_auth::secure_clear(st.password.text);
    if (res == TextFieldResult::Changed || res == TextFieldResult::Cancelled) {
        st.failed = false;
        LockOutputSurface *focus = nullptr;
        wl_surface *fs = lock_focused_surface(st);
        for (auto &up : st.surfaces)
            if (up->surface == fs)
                focus = up.get();
        if (focus)
            text_field_type_anim_sync(st.pw_anim, focus->animations, kLockOwnerDotBase, st.password.text);
        else
            st.pw_anim.chars.resize(text_field_utf8_len(st.password.text));
        request_all(st);
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
    if (hit(los->pill_button)) {
        try_authenticate(st);
        return;
    }
    if (hit(los->media_prev)) {
        st.app->media->previous();
        request_all(st);
    } else if (hit(los->media_play)) {
        st.app->media->play_pause();
        request_all(st);
    } else if (hit(los->media_next)) {
        st.app->media->next();
        request_all(st);
    }
}

void lock_timer_tick(LockState &st) {
    if (!st.active)
        return;
    if (st.failed && std::chrono::steady_clock::now() >= st.fail_clear_at) {
        st.failed = false;
    }
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
        state_.draw_wallpaper = [this, &app](const std::string &output_name, Node &root, int32_t w, int32_t h) {
            if (draw_wallpaper_)
                draw_wallpaper_(app, output_name, root, w, h);
        };
        state_.panel_gated_for = [&app](const std::string &output_name) {
            return lock_effective_enabled(app.cfg, output_name);
        };
        state_.echo_glyph = load_image_texture_first_existing({ASTRALIA_SHELL_INPUT_ECHO, "assets/electro.png"});
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
