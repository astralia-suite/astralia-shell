#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <algorithm>
#include <cstring>
#include <string_view>
#include <vector>

#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_registry.h"
#include "wayland/app/wayland_state.h"

#include "wayland/core/log.h"

#include "modules/lock/wayland/lock.h"

#include "wayland/render/gl.h"

namespace {

bool egl_has_extension(EGLDisplay display, std::string_view name) {
    const char *list = eglQueryString(display, EGL_EXTENSIONS);
    if (!list)
        return false;
    std::string_view rest(list);
    while (!rest.empty()) {
        size_t end = rest.find(' ');
        if (rest.substr(0, end) == name)
            return true;
        if (end == std::string_view::npos)
            break;
        rest.remove_prefix(end + 1);
    }
    return false;
}

std::vector<EGLint> robust_context_attribs(EGLDisplay display) {
    if (!egl_has_extension(display, "EGL_EXT_create_context_robustness"))
        return {};
    std::vector<EGLint> attribs = {EGL_CONTEXT_MAJOR_VERSION, 2};
    attribs.insert(attribs.end(), {EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT, EGL_LOSE_CONTEXT_ON_RESET_EXT});
    if (egl_has_extension(display, "EGL_NV_robustness_video_memory_purge"))
        attribs.insert(attribs.end(), {EGL_GENERATE_RESET_ON_VIDEO_MEMORY_PURGE_NV, EGL_TRUE});
    attribs.push_back(EGL_NONE);
    return attribs;
}

namespace output_detail {
void geometry(void *, wl_output *, int32_t, int32_t, int32_t, int32_t, int32_t, const char *, const char *, int32_t) {}
void mode(void *, wl_output *, uint32_t, int32_t, int32_t, int32_t) {}
void scale_event(void *data, wl_output *, int32_t factor) {
    static_cast<MonitorOutput *>(data)->output.scale = factor;
}
void name_event(void *data, wl_output *, const char *name) {
    static_cast<MonitorOutput *>(data)->output.name = name;
}
void description(void *, wl_output *, const char *) {}
void done(void *data, wl_output *) {
    auto *mon = static_cast<MonitorOutput *>(data);
    bool first_done = !mon->output.done;
    mon->output.done = true;
    if (!mon->activated && mon->app->egl_context != EGL_NO_CONTEXT)
        monitor_output_activate(*mon->app, *mon);
    if (first_done)
        lock_notify_output_added(*mon->app, mon->output.wl, mon->output.name.c_str());
}

const wl_output_listener &listener() {
    static constexpr wl_output_listener l{
        .geometry = geometry,
        .mode = mode,
        .done = done,
        .scale = scale_event,
        .name = name_event,
        .description = description,
    };
    return l;
}
} // namespace output_detail

namespace xdg_wm_base_listener_detail {
void ping(void *, xdg_wm_base *wm_base, uint32_t serial) {
    xdg_wm_base_pong(wm_base, serial);
}
constexpr xdg_wm_base_listener listener{.ping = ping};
} // namespace xdg_wm_base_listener_detail

void registry_global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    auto *state = static_cast<WaylandState *>(data);
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->compositor = static_cast<wl_compositor *>(wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 6u)));
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        state->layer_shell =
            static_cast<zwlr_layer_shell_v1 *>(wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1));
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        state->wm_base = static_cast<xdg_wm_base *>(wl_registry_bind(registry, name, &xdg_wm_base_interface, std::min(version, 6u)));
        xdg_wm_base_add_listener(state->wm_base, &xdg_wm_base_listener_detail::listener, nullptr);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        auto mon = std::make_unique<MonitorOutput>();
        mon->app = state;
        mon->output.registry_name = name;
        mon->output.wl = static_cast<wl_output *>(wl_registry_bind(registry, name, &wl_output_interface, std::min(version, 4u)));
        wl_output_add_listener(mon->output.wl, &output_detail::listener(), mon.get());
        state->outputs.push_back(std::move(mon));
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        state->seat = static_cast<wl_seat *>(wl_registry_bind(registry, name, &wl_seat_interface, 3));
        state->seat_caps.keyboard = &state->keyboard;
        state->seat_caps.pointer = &state->pointer;
        keyboard_attach_seat(state->seat_caps, state->seat);
    } else if (strcmp(interface, ext_idle_notifier_v1_interface.name) == 0) {
        state->idle.notifier =
            static_cast<ext_idle_notifier_v1 *>(wl_registry_bind(registry, name, &ext_idle_notifier_v1_interface, 1));
    } else if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0) {
        state->pointer.cursor_shape_manager =
            static_cast<wp_cursor_shape_manager_v1 *>(wl_registry_bind(registry, name, &wp_cursor_shape_manager_v1_interface, 1));
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->shm = static_cast<wl_shm *>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (strcmp(interface, hyprland_toplevel_export_manager_v1_interface.name) == 0) {
        state->toplevel_export_manager =
            static_cast<hyprland_toplevel_export_manager_v1 *>(wl_registry_bind(registry, name, &hyprland_toplevel_export_manager_v1_interface, 1));
    } else if (strcmp(interface, zwp_text_input_manager_v3_interface.name) == 0) {
        state->text_input_manager =
            static_cast<zwp_text_input_manager_v3 *>(wl_registry_bind(registry, name, &zwp_text_input_manager_v3_interface, 1));
    } else if (strcmp(interface, ext_session_lock_manager_v1_interface.name) == 0) {
        state->session_lock_manager =
            static_cast<ext_session_lock_manager_v1 *>(wl_registry_bind(registry, name, &ext_session_lock_manager_v1_interface, 1));
    }
}

void registry_global_remove(void *data, wl_registry *, uint32_t name) {
    auto *state = static_cast<WaylandState *>(data);
    auto it = std::find_if(state->outputs.begin(), state->outputs.end(), [name](const std::unique_ptr<MonitorOutput> &m) { return m->output.registry_name == name; });
    if (it == state->outputs.end())
        return;
    klog("output: '%s' removed", (*it)->output.name.c_str());
    wl_output *removed_wl = (*it)->output.wl;
    lock_notify_output_removed(*state, removed_wl);
    for (auto &m : state->overlays)
        m->on_output_removed(*state, removed_wl);
    if (state->last_pointer_monitor == it->get())
        state->last_pointer_monitor = nullptr;
    monitor_output_destroy(**it);
    state->outputs.erase(it);
}

} // namespace

const wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

bool bootstrap_egl(WaylandState &state) {
    state.egl_display =
        eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(state.display));
    if (state.egl_display == EGL_NO_DISPLAY)
        return false;
    if (!eglInitialize(state.egl_display, nullptr, nullptr))
        return false;
    eglBindAPI(EGL_OPENGL_ES_API);

    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE,
        EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,
        8,
        EGL_GREEN_SIZE,
        8,
        EGL_BLUE_SIZE,
        8,
        EGL_ALPHA_SIZE,
        8,
        EGL_NONE,
    };
    EGLint num_configs = 0;
    if (!eglChooseConfig(state.egl_display, config_attribs, &state.egl_config, 1, &num_configs) || num_configs == 0) {
        return false;
    }

    std::vector<EGLint> robust_attribs = robust_context_attribs(state.egl_display);
    if (!robust_attribs.empty()) {
        state.egl_context = eglCreateContext(state.egl_display, state.egl_config, EGL_NO_CONTEXT, robust_attribs.data());
        if (state.egl_context != EGL_NO_CONTEXT) {
            state.egl_context_attribs = std::move(robust_attribs);
            bool purge = std::find(state.egl_context_attribs.begin(), state.egl_context_attribs.end(), EGL_GENERATE_RESET_ON_VIDEO_MEMORY_PURGE_NV) != state.egl_context_attribs.end();
            klog("egl: robust context, lose on reset%s", purge ? ", video memory purge" : "");
            return true;
        }
        klog("egl: robust context creation failed, egl error 0x%04x, falling back to a plain context", eglGetError());
    }
    state.egl_context_attribs = {EGL_CONTEXT_MAJOR_VERSION, 2, EGL_NONE};
    state.egl_context = eglCreateContext(state.egl_display, state.egl_config, EGL_NO_CONTEXT, state.egl_context_attribs.data());
    if (state.egl_context == EGL_NO_CONTEXT) {
        klog("egl: OpenGL ES 2.0 context creation failed, egl error 0x%04x", eglGetError());
        return false;
    }
    return true;
}

bool renderer_bootstrap_init(WaylandState &state) {
    if (!egl_has_extension(state.egl_display, "EGL_KHR_surfaceless_context")) {
        const EGLint pbuffer_attribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        state.egl_rest_surface = eglCreatePbufferSurface(state.egl_display, state.egl_config, pbuffer_attribs);
        if (state.egl_rest_surface == EGL_NO_SURFACE)
            return false;
        klog("egl: no EGL_KHR_surfaceless_context, resting on a 1x1 pbuffer");
    }
    if (!eglMakeCurrent(state.egl_display, state.egl_rest_surface, state.egl_rest_surface, state.egl_context))
        return false;
    klog("gl: %s | GLSL %s", reinterpret_cast<const char *>(glGetString(GL_VERSION)), reinterpret_cast<const char *>(glGetString(GL_SHADING_LANGUAGE_VERSION)));
    gl_reset_detection_init();
    return state.renderer.init();
}
