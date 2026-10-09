#pragma once

#include <EGL/egl.h>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "wayland/app/ipc.h"
#include "wayland/app/module.h"

#include "wayland/config/visualizer_config.h"

#include "modules/visualizer/wayland/audio_capture.h"

#include "wayland/render/toplevel_window.h"

#include "service/wayland/input_service.h"

struct WaylandState;

struct VisualizerRenderThreadState {
    std::mutex mutex;
    std::condition_variable cv;
    bool shutdown = false;
    VisualizerParams params;
};

struct VisualizerState {
    ToplevelWindowBase base;
    VisualizerAudioCapture capture;

    EGLConfig egl_config = nullptr;
    EGLContext render_context = EGL_NO_CONTEXT;
    std::thread render_thread;
    std::unique_ptr<VisualizerRenderThreadState> thread_state;
};

void visualizer_shutdown(VisualizerState &state);

void visualizer_apply_params(VisualizerState &state, const VisualizerParams &params);

void visualizer_toggle(VisualizerState &state, WaylandState &app);

void visualizer_handle_key_event(VisualizerState &state, WaylandState &app, const KeyEvent &event);

std::vector<astralia::ShellBinding> visualizer_shell_bindings(VisualizerState &visualizer, WaylandState &state);

std::unique_ptr<Module> make_visualizer_module();
