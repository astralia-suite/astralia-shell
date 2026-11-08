# Wayland render threads and EGL

GPU-heavy overlays on their own thread with a share context. Sources: `src/backend/wayland/render/gl.cpp`, `src/backend/wayland/app/wayland_registry.cpp`, `src/modules/visualizer/wayland/`.

- [wayland] Run a GPU-heavy overlay on its own thread with a share-context `EGLContext`. It must leave the poll loop to stop stalling it.
- [wayland] A render thread and the main thread never hold one `EGLSurface` current together. The render thread owns it while open.
- [wayland] Drop the main thread's `EGLSurface` via `rest_egl_current` before a render thread takes over. Skipping it lost `eglMakeCurrent` with `EGL_BAD_ACCESS`.
- [wayland] Rest the shared context on `egl_rest_surface`, never a module's surface. It is surfaceless or a 1x1 pbuffer, since a bar can unplug.
- [wayland] A share context uses the main context's exact `egl_context_attribs`. Different reset-notification attributes fail with `EGL_BAD_MATCH`.
- [wayland] `gl_make_current` consumes the real EGL error. A later `eglGetError` misleadingly reads `EGL_SUCCESS`.
- [wayland] Do not drive a render-thread window from the poll-thread `FrameClock`. On Mesa it deadlocked the frame pump after one frame.
