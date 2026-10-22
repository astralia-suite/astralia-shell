#pragma once

#include <EGL/egl.h>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "wayland/app/ipc.h"
#include "wayland/app/module.h"
#include "wayland/config/visualizer_gl_config.h"
#include "wayland/render/toplevel_window.h"

#include "modules/visualizer/audio_capture.h"

#include "service/wayland/input_service.h"

struct WaylandState;

namespace astralia::visualizer {

struct RenderThreadState {
    std::mutex mutex;
    std::condition_variable cv;
    bool shutdown = false;
    VisualizerParams params;
};

struct State {
    ToplevelWindowBase base;
    AudioCapture capture;

    EGLConfig egl_config = nullptr;
    EGLContext render_context = EGL_NO_CONTEXT;
    std::thread render_thread;
    std::unique_ptr<RenderThreadState> thread_state;
};

void visualizer_shutdown(State &state);

void apply_params(State &state, const VisualizerParams &params);

void toggle(State &state, WaylandState &app);

void handle_key_event(State &state, WaylandState &app, const KeyEvent &event);

std::vector<astralia::ShellBinding> shell_bindings(State &visualizer, WaylandState &state);

std::unique_ptr<Module> make_module();

} // namespace astralia::visualizer
