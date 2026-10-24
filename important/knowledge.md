# `astralia-shell` development knowledge

## Description

Hard-won programming techniques and pitfalls from astralia-shell's development.

Can be updated if found new knowledge that supersedes old ones, or genuinely new ones.

Design choices belong in `convention.md` and the code, not here.

## Rule

One statement + One explanation, ≤ 20 words each.

Drop an entry once newer knowledge fully supersedes it.

Entries describe the merged code base; name a symbol only while it exists.

## Entries

Entries without a tag apply to every backend. Tags `[wayland]` and `[x11]` mark backend-specific rules; where they disagree, the tag says which one applies.

## Shared: process and build

- Tests set `XDG_STATE_HOME` to a temporary directory first. `log` opens the real state file on first use and would fill it.
- Point tests at a scratch `XDG_CONFIG_HOME`. The real config stays untouched.
- A session-named path comes from `WAYLAND_DISPLAY`, else `DISPLAY`. Hyprland also exports `DISPLAY` for XWayland, so the order matters.
- Link a core library into the executable with `link_whole` and `export_dynamic`. Plugins resolve those symbols at `dlopen`.
- Build plugins with hidden visibility and `--gc-sections`, exporting only the factory. The linker then drops unused functions.
- Never `dlclose` a plugin. Its vtables and registered handlers outlive any point where closing would be safe.
- Link a backend's static library with `link_whole` and `pic: true`. The plugin and its unit-test executable then share it.
- Declare each `shared_module` in the root `meson.build`. A subdirectory puts the `.so` away from the loader's search path.
- Compile generated protocol `.c` files into one library only. Two copies define every interface twice at link.
- Prefix a backend's headers with its directory when names collide with shared ones. A first-match include search would pick the wrong copy.
- Keep pure logic apart from EGL, GL and `xcb` code. The test binary then links no display library.
- A generically named `constexpr` can collide with an identical name in an unrelated header. Modules that never include each other can still meet transitively.
- Never start a second shell instance from a test inside the user's session. It would compete for the real lock and socket.
- Live-test lock and input-grabbing code only with a recovery path ready. A stuck grab hangs the keyboard and forces a reboot.
- Never `pkill -f` a pattern the running shell command also contains. Kill the PID captured from `$!` instead.
- zsh breaks `${var^}` and unquoted word splitting inside heredoc loops. Generate files from a script instead.
- `grim` captures the whole screen, private windows included. Delete each screenshot right after viewing it.
- Writing literal Unicode escapes through tool calls is unsafe. The JSON layer can swap them for glyph bytes; verify with `od -c`.

## Shared: structure and refactoring

- Guard a service-signal slot with the owner's alive flag. `Signal` has no disconnect, so a destroyed owner would be called.
- Delete a removed UI feature's click kinds, state and handlers in the same change. Leftovers otherwise read as live code.
- Migrate every call site when a refactor changes a shared function's contract. Stragglers silently skip the new behaviour.
- Move call sites to the new names instead of keeping alias headers. Aliases hide which file a symbol comes from.
- Size a per-enum table to its enum. A too-short table silently zero-fills.
- A canvas draws images at their natural size. Scale to the slot yourself, or a large icon covers its surroundings.
- Tint photos, icons and gauges with white. A coloured tint recolours or dims the image; only monochrome masks take a colour.
- Score colour clusters by `sqrt(area) * chroma`. A linear area score let the pale majority hide small vivid regions.

## Shared: threads, D-Bus and processes

- Never read D-Bus properties synchronously on the reactor thread. Use `dbus_get_all_async`; a stalled BlueZ once froze the whole shell.
- No reactor-thread callback may make a blocking D-Bus call. A synchronous Bluetooth read froze the shell for 18 seconds.
- Do not build textures or rasterize text from a D-Bus callback. Build lazily in the paint path instead.
- Hold an async reply as an `sdbus::Slot` member. Destroying the owner then cancels the reply instead of calling a dead `this`.
- Coalesce D-Bus-signal refreshes to one call in flight plus a repeat flag. A signal burst otherwise queues one full refresh each.
- Never destroy an sdbus proxy inside its own async reply callback. Prune per-device proxies from the signal-match handler instead.
- Hold one sdbus proxy per fixed object, but keep changing paths one-off. Caching per-reconnect paths grows without bound.
- A local proxy destroyed right after firing an async call corrupts the connection it borrowed. Cache one proxy per object path.
- `Bus` polls both the bus fd and its `eventFd`. Sync calls queue signals internally, and only `eventFd` wakes the loop.
- A timer- or signal-driven service must emit only on a real state diff. A once-a-second tick otherwise repainted every surface.
- Keep earliest-deadline logic out of list order. Entries with different timeouts need the timer to take the minimum deadline.
- Compare a config file the shell writes after parsing, before emitting. The shell's own atomic write trips its `inotify` watch.
- Watch a config file's directory with `inotify`, filtered by name. Editors save by rename, which drops a watch on the file.
- A file writer must create its own target directory. A fresh install otherwise fails the first save silently.
- Renaming a persisted config value needs a load-time mapping. Users otherwise silently lose the setting.
- `~` is a display convention, never a real path. `std::filesystem` never expands it, so expand before use.
- Config path defaults must already be absolute. A default returned unexpanded is scanned directly and finds nothing.
- Never allocate in a forked child before `exec`. `fork()` may copy a lock another thread held.
- Spawn with `posix_spawn`, not `fork`, on the reactor thread. glibc takes allocator locks other threads hold.
- Ignoring `SIGCHLD` and calling `waitpid` cannot coexist. Ignoring is process-wide and lets the kernel reap, breaking `waitpid` everywhere.
- Fire-and-forget processes double-fork instead of relying on global reaping. The grandchild reparents to init and is reaped there.
- Spawned children need an empty signal mask and default `SIGPIPE`. `exec` inherits the blocked and ignored signals of the shell.
- A blocking `waitpid` thread beats watching a child's pipe in the poll loop. `poll()` once missed readiness for tens of seconds.
- Kill a cancelled background process instead of detaching it. Retyped searches otherwise pile up competing processes.
- Reusing a handle across restarts needs a generation counter. A cancelled worker can wake later with a stale result.
- Track "request in flight" with your own flag. The worker resets `pid` to `-1` the instant it marks the run done.
- Never score an async result against a live mutable field. Freeze the input into its own field at start.
- Signal every worker's stop flag before joining any. Signalling and joining one at a time made shutdown block serially.
- Tear a module's background thread down in its destructor, not only its close path. A joinable thread's destruction calls `std::terminate`.
- A detached reader thread must own what it touches through a `shared_ptr`. The owner may be destroyed before the child exits.
- Never destroy the Wayland state at shutdown. Detached threads keep references into it and abort the process when it is freed.
- Expect threads you did not spawn. Mesa, PipeWire and Pango each start their own, so the process is multithreaded anyway.
- Capture a value synchronously at the action site. Deferring to the next repaint left panels opening at position zero.
- Capture click coordinates with the click event itself. The live shared pointer position may belong to another monitor.
- Set a reactive `*_changed` flag on every path that changes the value. One mutator forgetting it breaks only that path.
- An optimistic local write can suppress the `*_changed` flag it should trigger. Raise the flag at the write.
- Wipe password buffers with `explicit_bzero` after responding or cancelling. `std::string::clear` leaves the bytes in the heap.
- A connect-to-socket liveness probe is unreliable against a leftover socket file. Use a `flock`-guarded lock file instead.
- Take the single-instance `flock` before `daemonize()`. The child inherits the lock, so it survives the parent's exit.
- Ignore `SIGPIPE` at startup. A write to a closed peer otherwise terminates the process without a core.
- Install a `std::set_terminate` hook next to the crash handler. `daemonize()` sends `stderr` to `/dev/null`, hiding the exception text.
- A `backtrace_symbols_fd` trace maps to source only with `addr2line` against the exact crashed binary. A rebuild invalidates the offsets.
- The poll loop must `continue` on `EINTR` and log any other `poll()` error. Breaking on `EINTR` exited the whole process.
- Never size an allocation from an on-disk header without checking the file size. A corrupt cache header made `new[]` throw on a worker.
- libjpeg reports errors by `longjmp`, skipping destructors. Hold buffers in plain or `volatile` pointers freed in the `setjmp` branch.
- libjpeg's default `error_exit` calls `exit()`. Install a handler that jumps back and returns `nullptr`.
- Give every bundled asset an installed-path-plus-source-tree fallback. A bare relative path resolves against the daemon's working directory.
- Set brightness through `brightnessctl`, never a direct `sysfs` write. The file is root-only without a `uaccess` rule.
- Resolve a dock icon through a `.desktop` id or `StartupWMClass` to `Icon`. The raw window class collapses unresolved apps onto one placeholder.
- A bounded `stat()` search beats a subprocess over a handful of candidates. Icon lookup needs existence checks only.
- Intel iGPUs often lack a GPU `hwmon` yet expose `gt_act_freq_mhz`. `find_gpu_clock_sensor` falls back to scanning `/sys/class/drm`.
- An async result polled on a throttle lands a throttle period late. Poll every tick while a request runs.
- Reset the activity clock when idle management is re-enabled or its timeout drops. A stale `last_activity` fires the screensaver instantly.
- Binding a PipeWire node listener alone delivers no live parameter updates. Call `pw_node_subscribe_params()` explicitly.
- A client's callback confirming a write is not proof the state changed. Device-backed nodes need writes routed through the parent Device's Route.
- A mutex must cover the read side of a shared buffer. Ring-buffer reads outside the lock raced the PipeWire thread.
- A second writer to shared per-bin state should prompt an audit of every existing writer. One recomputed off-mutex and became a race.
- Cap glibc arenas and pin the `mmap` and trim thresholds. A decode burst otherwise inflates RSS for good.
- Call `malloc_trim(0)` after bursts like decodes or broad searches. glibc keeps freed small chunks, pinning several megabytes otherwise.
- Slow idle heap growth is glyph-cache warm-up plus fragmentation, not a leak. `heaptrack` showed live heap flat after three idle minutes.

## Shared: compositors

- i3 exports `I3SOCK`, not `I3_SOCK`. The socket path also lives in the `I3_SOCKET_PATH` property on the X root window.
- Subscribe to i3 `workspace` and `window` events on a dedicated socket. A refresh per event replaces watching root properties.
- An i3 workspace exists only while it holds windows or is visible. The list is not a fixed ten, so pills need `workspace_slots`.
- Derive i3 tiled rects from `layout` and `percent`, not `GET_TREE` rects. i3 skips rendering hidden workspaces, leaving them stale.
- Wait for the i3 `RUN_COMMAND` reply before querying again. Otherwise the tree read can precede the command's effect.
- Ignore focus loss briefly after an i3 command, then refocus. i3 hands focus to a client on workspace switches.
- Hyprland emits `createworkspacev2` and `activewindowv2` before `workspacev2`. Treat any structural event in a batch as a full refresh.
- Refresh on `activewindowv2` too. Focus changes bump every client's `focusHistoryID`, so UI ordered by it goes stale.
- Hyprland emits no event for a tiled reorder inside a workspace. Client re-reads are opt-in through `watch_client_order`.
- Give every Hyprland request on the poll thread a socket timeout, read as "no change". An empty reply parsed as no clients blanks the dock.
- Hyprland's classic `dispatch` strings are deprecated for Lua. `HyprlandCompositor::dispatch` takes a `hl.dsp.*` expression.
- `j/getoption <name>` works over the existing request socket. No need to shell out to `hyprctl`.
- Pass `global=true` on every tile and focus call of a workspace grid spanning monitors. Hyprland otherwise remaps ids onto the focused monitor's page.
- Refresh compositor state before an overlay reads it on open. Event-driven state can be stale by then.
- Compositor state is not ready when the first monitor's surface is created. Sizing from it needs a later catch-up.

## Shared: images, fonts and text

- Tabler codepoints collide with Codicons in the Nerd Font. Name the `tabler-icons` Pango family explicitly for icons.
- Register app fonts before the first `Text` exists. Pango snapshots fonts on creation and misses ones added later.
- A glyphless icon codepoint fails silently as an empty texture. Check icon constants against the actual font.
- Size text and icon textures from fixed font metrics, not ink extents. Ink sizing made the baseline jitter between strings.
- A per-glyph text field advances each cell by the font's fixed advance. Summing ink widths collapses narrow glyphs.
- A glyph missing from the primary font shifts the whole line's baseline. Pango's fallback inflates ascent; use the em dash over `U+00B7`.
- `show_layout`'s current point is the top-left corner, not the baseline. Adding ascent doubled the offset.
- Cairo output is premultiplied alpha but the blend convention is straight alpha. Mixing them squares alpha at antialiased edges.
- A `Texture`'s size is device pixels (`logical * scale`). Divide by `scale` when laying out, or HiDPI blocks come out 2x.
- A decode resolution must be device pixels, not logical. Decoding at logical size upscaled images blurrily on scale 2.
- A pre-upload downsample derives its size from the source aspect ratio. Squashing to the box bakes in a stretch no crop undoes.
- `decode_pixels` fits an SVG inside its box keeping the aspect. Give a height-only fit a huge width bound.
- `decode_pixels` allocates every buffer with `new[]`, stb included through `STBI_MALLOC`. Each backend then adopts the buffer without a copy.
- `decode_pixels` returns SVG renders premultiplied and every other format straight. Check `Pixels::premultiplied` before converting to cairo's ARGB32.
- Give a JPEG decode a target size to use libjpeg `scale_denom`. Reduced-size decoding skips most pixels and memory.
- Font hinting differs between icons and text. `HINT_STYLE_NONE` keeps Tabler strokes thick while hinted text stays crisp.

## Wayland: rendering

- [wayland] Shaders are `#version 100` on one OpenGL ES 2.0 context. Both context creation sites must change together.
- [wayland] Clip with scissor rects plus a corner inset, not a stencil buffer. It is correct while nothing visually touches a rounded edge.
- [wayland] A scissor clip helper needs a stack once clips nest. An unconditional disable on destruction wiped the outer clip.
- [wayland] Floor and ceil each scissor edge independently. Flooring the origin then rounding the size can drop the last row.
- [wayland] A group clips only when it is not itself scaled or rotated. Scissor rects ignore the model transform.
- [wayland] Overlapping translucent shapes of one colour double-darken. Draw them opaque over an opaque base instead.
- [wayland] Draw a multi-shape outline as two stacked layers. The whole silhouette takes the border colour, the inset one the fill.
- [wayland] `draw_texture_rect` rounds its `x` and `y`, but shapes do not. Round the shared edge first or a texture lands a pixel off.
- [wayland] Fractional texture positions blur every glyph. `GL_LINEAR` blends edge texels at half-pixel offsets, so round before drawing.
- [wayland] `GlCanvas` draws immediately, so call order is paint order. `set_opacity()` applies to later draws only.
- [wayland] Shape edges use a linear ramp one device pixel wide: `clamp(0.5 - d / length(vec2(dFdx(d), dFdy(d))), 0.0, 1.0)`. A `smoothstep` window reads as stair-stepping on curves.
- [wayland] Shape shaders output straight alpha, `vec4(rgb, a * coverage)`. Multiplying `rgb` by coverage as well squares the edge under `GL_SRC_ALPHA`.
- [wayland] Draw analytic shapes on a quad padded by `edge_pad` and pass the unpadded size as a uniform. A quad cut at the rect clips the ramp.
- [wayland] A rounded-rectangle distance needs the interior `min(max(q.x, q.y), 0.0)` term. Otherwise a border wider than the radius fills the rect.
- [wayland] `draw_rounded_rect` always reads its border colour, even at zero width. Passing `nullptr` is a null read.
- [wayland] A shared `Renderer` resets itself in `begin_frame()`. Callers otherwise leaked stale opacity into the next paint.
- [wayland] A fresh share context starts with `GL_BLEND` disabled. Raw GL paths enable it once per context.
- [wayland] `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` squares alpha over a transparent-cleared surface. Use `glBlendFuncSeparate` with `GL_ONE` for alpha.
- [wayland] `smoothstep` needs `edge0 < edge1`. Mesa tolerates reversed edges, NVIDIA may not; invert with `1.0 - smoothstep(lo, hi, x)`.
- [wayland] Declare `precision highp float` unconditionally in fragment shaders. `mediump` is narrow on NVIDIA and overflowed a thunder shader.
- [wayland] Run a `float(x == x)` NaN guard before `clamp`. `clamp` launders `NaN` to an endpoint.
- [wayland] `pow(0.0, 0.0)` is undefined: NVIDIA returns `1.0`, Mesa Intel `NaN`. Guard bases with `max(base, 1e-4)`.
- [wayland] GLSL ES 1.00 needs compile-time loop bounds. Use a fixed `MAX_*` cap with an early `break`.
- [wayland] GLSL ES 1.00 has no `matNxM` types, array constructors or whole-array assignment. Assign per element after a bare declaration.
- [wayland] `glTexImage2D` internal format must equal the data format on ES 2.0. `GL_RGBA8` compiles but is the wrong token.
- [wayland] ES 2.0 needs `_EXT` tokens such as `GL_RED_EXT` and `GL_MAX_EXT`. Include `gl2ext.h` after `gl2.h`, which defines `GL_APIENTRY`.
- [wayland] `GL_UNPACK_ROW_LENGTH` needs the `GL_EXT_unpack_subimage` string probe. It is not a core ES 2.0 token.
- [wayland] Exact texel reads use `texture2D` with `GL_NEAREST`. Add `+0.5` only to integer indices; `gl_FragCoord` already sits at `i + 0.5`.
- [wayland] Float additive accumulation needs `GL_OES_texture_float`, `GL_EXT_color_buffer_float` and `GL_EXT_float_blend`. Probe the extension string.
- [wayland] Skip the CPU BGRA swizzle for shm captures. `GL_EXT_texture_format_BGRA8888` uploads them directly.
- [wayland] A custom multi-pass shader effect bypasses `GlCanvas`. Give it its own programs and FBOs, called from the module's paint.
- [wayland] A single-pass module shader can ride `Renderer::draw_custom`. Reuse `renderer/quad.vert` and `quad_vbo_` instead of owning FBOs.
- [wayland] Create a static quad's buffer once with its program. Per-frame `glGenBuffers` is driver churn.
- [wayland] Render a per-frame multi-tap fullscreen pass to a downsampled FBO. At native resolution it stalls the poll loop.
- [wayland] Rasterize once and cache by key for any procedural drawing. Continuous inputs such as percentages need bucketing or the cache grows unbounded.
- [wayland] The default EGL swap interval blocks the poll thread on NVIDIA. `gl_make_current` sets `eglSwapInterval(display, 0)` on every bound surface.
- [wayland] Frame callbacks already pace every surface, so a zero swap interval causes no tearing. Skip the frame on any `EGL_FALSE`.
- [wayland] Never drive GL for a surface the compositor stopped compositing. A covered wallpaper would otherwise paint into the lock surface.

## Wayland: animation and frames

- [wayland] `request_frame` defers a mapped surface's repaint to `frame_done`. Inline `eglSwapBuffers` from a D-Bus handler froze the shell.
- [wayland] A deferred commit needs a one-pixel `wl_surface_damage_buffer`. Hyprland skips scheduling a frame for an undamaged commit.
- [wayland] `frame_done` arms the next frame callback before calling `draw()`. The paint's own commit must carry the next request.
- [wayland] A closed full-screen overlay must no-op its `request_frame`, gated on `open`. Blanket fan-outs otherwise repaint idle surfaces.
- [wayland] A closing overlay requests its frame before its fade completes. `on_complete` flips `open`, and a trailing call then never arms.
- [wayland] A host requests its frame before `model.close()`. With animations off the close ends inside the call, so a later request is dropped.
- [wayland] `redraw_all_monitors` pokes per-monitor modules only. An open overlay reacting to an event needs its own `request_frame` loop.
- [wayland] A paint that re-arms only while a tween is active needs every time-varying element tween-driven. A wall-clock bar froze otherwise.
- [wayland] `AnimationManager` advances only when `tick()` runs each frame. A panel that forgets freezes, `on_complete` callbacks included.
- [wayland] Repeated `tick()` calls per instant are safe. Progress comes from wall-clock time, not accumulated delta.
- [wayland] Two `animate()` calls sharing an owner id cancel each other. Give each animated property its own id.
- [wayland] An animation's `on_complete` that destroys its surface fires inside paint's `tick()`. Re-check validity after `tick()` or defer the destroy.
- [wayland] Defer teardown from an `on_complete` through `DeferredCall`. Freeing inline destroys the manager mid-iteration.
- [wayland] A global instant switch cannot reach a self-re-arming `on_complete` chain. It recurses forever, so such a chain checks it itself.
- [wayland] An animated per-frame layout value needs a snap-on-first-value sentinel. Otherwise the first frame tweens from a stale default.
- [wayland] A tweened value must not derive from another mid-tween value. It re-targets every frame instead of converging.
- [wayland] `AnimationManager` has no delay primitive. Chain a no-op tween whose `on_complete` starts the real one.
- [wayland] Drive a scripted entrance from one linear progress value and a pure sampling function. It then needs no delay primitive and is testable.
- [wayland] A per-element opacity tween must not run during a container fade. Two alpha ramps multiply into an unintended curve.
- [wayland] Tween only opacity on a fading, resizing surface and snap geometry at the endpoints. One tween for both rescaled the content.
- [wayland] `MarqueeTextState::marqueeing` means scrolling, not overflowing. Clip on texture width over box width.
- [wayland] Vary a cell-snapped column's fall rate by skipping ticks with a fractional accumulator. A fractional step desnaps glyphs from the grid.

## Wayland: media and decode

- [wayland] Include every decode-shaping parameter, such as `fit`, in the `.rgba` cache key. Otherwise old-shape frames are reused silently.
- [wayland] Decode frames into a `.tmp` directory and rename on success. A killed decode left a partial set that later runs trusted.
- [wayland] Remove the rename target first. `rename(2)` into a non-empty directory fails, and a stale `.png` cache blocked every promotion.
- [wayland] Prune a frame cache oldest-first and touch files on read. Use order then drives eviction.
- [wayland] Pick the hardware decoder from a preference list of `AVHWDeviceType`s. Try `CUDA`, then `VAAPI`, then software.
- [wayland] Zero-copy texture import is vendor-specific. `VAAPI` uses `DMA-BUF` and `EGLImage` on Mesa; `CUDA` interop is separate.
- [wayland] `AVCodecContext::get_format` takes a plain function pointer. Pass the wanted hardware format through `opaque`.
- [wayland] Build the decode filter graph from the first decoded frame. The hardware transfer format varies by driver.
- [wayland] Loop in-process video with a seek and flush. `av_seek_frame` plus `avcodec_flush_buffers` on EOF replaces the CLI loop flag.
- [wayland] A decoder can hold a frame back until the next packet or flush. Send a `nullptr` packet and drain before flushing.
- [wayland] Pace playback by each frame's `pts` against a wall-clock anchor. A fixed sleep per frame throttles decode, not display.
- [wayland] The zero-copy `VAAPI` path skips the filter graph. An `fps=` filter there changes nothing.
- [wayland] Scale a thumbnail decode inside the filter graph. `decode_first_frame` emits `scale=W:H,format=rgba`, so frames are KB, not MB.
- [wayland] Pin `thread_count = 1` for first-frame decodes. Frame threading buys nothing and multiplies arenas.
- [wayland] Avoid one detached decode thread per tile. About 35 concurrent decodes kept eight 64 MB arenas.
- [wayland] A sync-from-config function uploading only non-empty paths must clear the texture on an empty path. Reset it and bump the generation.
- [wayland] Clearing a texture in memory does not repaint. Request a frame on both branches, or the old picture stays.
- [wayland] Capture a `std::weak_ptr<int>` lifetime token in decode closures. Per-monitor state can be freed on hotplug before the closure runs.

## Wayland: render threads and EGL

- [wayland] Run a GPU-heavy overlay on its own thread with a share-context `EGLContext`. It must leave the poll loop to stop stalling it.
- [wayland] A render thread and the main thread never hold one `EGLSurface` current together. The render thread owns it while open.
- [wayland] Drop the main thread's `EGLSurface` via `rest_egl_current` before a render thread takes over. Skipping it lost `eglMakeCurrent` with `EGL_BAD_ACCESS`.
- [wayland] Rest the shared context on `egl_rest_surface`, never a module's surface. It is surfaceless or a 1x1 pbuffer, since a bar can unplug.
- [wayland] A share context uses the main context's exact `egl_context_attribs`. Different reset-notification attributes fail with `EGL_BAD_MATCH`.
- [wayland] `gl_make_current` consumes the real EGL error. A later `eglGetError` misleadingly reads `EGL_SUCCESS`.
- [wayland] Do not drive a render-thread window from the poll-thread `FrameClock`. On Mesa it deadlocked the frame pump after one frame.
- [wayland] A fullscreen vertex shader emitting only `gl_Position` pairs with a fragment shader using `gl_FragCoord`. A `vUv` varying would be undefined.
- [wayland] A splat accumulator must be a `GL_FLOAT` target. Hundreds of splats per pixel clamp at `1.0` in `UNORM`.
- [wayland] Flatten ported multi-file shaders at authoring time. The shell has no runtime shader preprocessor.
- [wayland] Scale an arc gauge's stroke by the reference stroke-to-diameter ratio when it grows. A fixed stroke turns into a hairline.

## Wayland: surfaces and protocol

- [wayland] Wayland cannot tell which output the pointer is over. Track a best-effort hint from your own surfaces' enter and motion events.
- [wayland] Clear the pointer hint when its output is removed. A stale pointer to a freed output caused a use-after-free.
- [wayland] Optional protocol events need sane fallbacks, not zero. Some compositors never send `repeat_info`, and `0/0` disables key repeat.
- [wayland] A coordinate from one layer-shell surface is invalid on another without translating margins. Origins differ.
- [wayland] Hover-driven surface changes touch size only, never margin or exclusive zone. A margin change repositioned mid-hover and looped.
- [wayland] `exclusive_zone` excludes the same-edge margin. The compositor adds it already, so including it reserves the space twice.
- [wayland] A bind's reply is a second round-trip. Do one extra roundtrip before reading output state.
- [wayland] A live-update IPC event may lack a field only a full snapshot has. Check whether an adjacent event carries it and can cache it.
- [wayland] Start the polkit agent only after keyboard input and a GLib loop exist. An agent that cannot prompt fails every `pkexec`.
- [wayland] A struct member cannot share a name with a protocol type in the same header. `xdg_surface *xdg_surface` breaks lookup downstream.
- [wayland] Create a real `xdg_toplevel` on open and destroy it on close. A mapped transparent toplevel still shows in switchers.
- [wayland] `ToplevelWindowBase` has no resize callback. A module with a per-size buffer compares live width and height each paint.
- [wayland] Reset a torn-down window's `FrameClock` fields as well as its handles. A stale non-null callback silences `request_frame` forever.
- [wayland] Release a pending `wl_callback` with `wl_callback_destroy`, never by nulling. `wl_surface_destroy` does not free it.
- [wayland] Release an `EGLSurface` from the current context before `wl_egl_window_destroy`. A current surface defers teardown into freed memory.
- [wayland] Call `gl_release_if_current` before every `EGLSurface` teardown. It unbinds only that surface, keeping the context bound surfacelessly.
- [wayland] Change a bottom-anchored layer surface's size and buffer in one commit. A separate commit dropped hover.
- [wayland] Keep layer overlays mapped across same-output toggles. Recreating a same-namespace surface left it uncomposited or unrouted on Hyprland.
- [wayland] A destroy-on-close toplevel can reuse an earlier window's address. State keyed by address must clear when the window goes.
- [wayland] A mostly click-through layer surface takes clicks through a `wl_region` union of sub-rects. Rebuild the union whenever the rects change.
- [wayland] A `PerMonitorModule` pointer move fires on every monitor with the same coordinates. Guard hover on `pointer.focused_surface` being its own.
- [wayland] The pointing-hand cursor needs `wants_hand_cursor()` on both `Module` and `PerMonitorModule`. Otherwise bars keep the arrow.
- [wayland] Every panel with exclusive keyboard interactivity needs its own key-dispatch arm. Nothing enforces the two stay in sync.
- [wayland] A modifier-aware shortcut matches `KeyEvent::base_sym`, not `text`. `Shift`+`1` is `!` in `text`.
- [wayland] A hover highlight clears on lost surface focus, not only on motion. Stolen focus leaves a stale hovered index.
- [wayland] A full-output overlay that paints its own opaque backdrop must paint one empty frame after release. A closed surface keeps its last buffer.
- [wayland] A per-monitor `apply_config` reads its `new_cfg`, never `app.cfg`. The latter is assigned after the fan-out.
- [wayland] A per-monitor `create_surface()` must not gate on config state. It runs once, so a startup check broke toggle-on.
- [wayland] Every per-monitor `destroy()` drops its pending frame callback. A late `frame_done` otherwise runs on freed state.
- [wayland] A per-monitor layer surface's `.closed` handler is a no-op. `registry_global_remove` owns cleanup; exiting there kills the whole shell.
- [wayland] `registry_global_remove` tells overlays an output is gone via `Module::on_output_removed`. Otherwise they keep a dangling `bound_output`.
- [wayland] A per-monitor text-input client clears its own focus in `destroy()`. `PerMonitorModule::destroy()` bypasses `on_output_removed`.
- [wayland] An `xdg_popup` needs the parent layer surface, an anchor rectangle and the triggering input serial. Without the serial the compositor denies the grab.
- [wayland] A popup's `done` event dismisses it. Destroy the popup and notify the panel, since the compositor has already dropped the grab.
- [wayland] `ext-session-lock` withholds `locked` until every output presents a non-null buffer. Paint all lock surfaces before flushing.
- [wayland] Grab a transition snapshot with `glCopyTexImage2D` after `flush` and before the swap. The texture then outlives the frame.
- [wayland] Set keyboard interactivity to none and commit on overlays that outlive the lock. Otherwise they keep exclusive focus.
- [wayland] Never release the session lock inside a transition `configure` handler. Defer it with `DeferredCall`.
- [wayland] Poll a deadline from the module timer to finish a stuck transition. The lock request then still runs.
- [wayland] `unlock_and_destroy` needs a `wl_display_roundtrip` before teardown. Otherwise the server may kill the client with a protocol error.
- [wayland] Ack a lock-surface `configure` before any commit and on every resend. Defer the `wl_egl_window` resize past the handler.
- [wayland] Do not gate the startup overlay's `init_egl` loop on `surface()`. The lock owns none until locked.
- [wayland] A `pam_start_confdir` service needs an `account` rule. Without it `pam_acct_mgmt` returns `PAM_PERM_DENIED` on a correct password.

## X11 backend

- [x11] Never name a backend file like a shared one, such as `core/clipboard.h`. The backend's `core/` include root shadows `src/core/`.
- [x11] Answer `SelectionRequest` from the owner window even while it is unmapped. Selection events are not maskable and ownership outlives the map state.
- [x11] Use a `CLOCK_BOOTTIME` `timerfd` for wall-clock timers. Monotonic clocks pause in suspend, leaving the clock stale after resume.
- [x11] Never wipe the EWMH connection after `xcb_ewmh_init_atoms_replies` fails. Its failure path already frees everything, so wiping double-frees.
- [x11] Strings with embedded NULs, such as `WM_CLASS`, need an explicit length. A `std::string_view` from `const char*` stops at the first NUL.
- [x11] Under i3, dock windows ignore requested position and struts. Inset content inside a full-width ARGB dock instead.
- [x11] A 32-bit window needs its own colormap and a border pixel. Otherwise creation fails with `BadMatch` against the root depth.
- [x11] Round-trip before `xcb_disconnect`. It does not sync, so Xorg drops unprocessed requests and loses exit cleanup.
- [x11] Never set `ESETROOT_PMAP_ID` on the shell's own pixmap. Wallpaper setters kill its owner, which would disconnect the whole shell.
- [x11] Reset the root background and delete `_XROOTPMAP_ID` when the wallpaper painter is destroyed. Otherwise the root keeps the image alive.
- [x11] Never `cairo_device_finish` a cairo-xcb device on the shell connection. The device is shared, so finishing it breaks every window.
- [x11] RandR notify events carry no event window. Route them with `EventLoop::on_event`, since the root handler belongs to another service.
- [x11] Let one `OutputService` own the RandR notify. `EventLoop::on_event` keeps a single handler per type, so a second owner replaces it.
- [x11] Drain the xcb queue before each `poll`, never inside a poll source's `prepare`. A handler may add a source and invalidate iteration.
- [x11] Take window geometry from `CompositorClient` positions minus the monitor's work area. `reserved` is `{left, top, right, bottom}`.
- [x11] Overlays take input focus, never an active keyboard grab. A grab blocks the window manager's hotkeys, so toggles never arrive.
- [x11] Read xkb modifiers from each key press's `state` field. Overlays opened by a Shift hotkey miss its release.
- [x11] Map a popup beside a panel with `show(false)`, never focus. Taking focus fires the panel's focus-out close; its `owner_events` grab still routes clicks.
- [x11] Catch a desktop right-click on a bottom `InputOnly` override-redirect window per output. Only one client may select `ButtonPress` on the root.
- [x11] Reuse windows on output changes and unmap the extras. `EventLoop` cannot remove a window handler.
- [x11] Hide windows and timers instead of destroying them. `EventLoop` cannot remove either, so destroyed ones would dangle.
- [x11] Grab the pointer with `owner_events` for a context menu. A click in another client's window then closes it instead of reaching that client.
- [x11] Call `EventLoop::reschedule()` when an event moves a timer's deadline earlier. Deadlines are only recomputed after firing.
- [x11] Drive GLib through an `EventLoop` poll source. Polkit and GDBus use changing fds that fixed fd watches miss.
- [x11] SNI items signal `NewIcon` and `NewStatus`, rarely `PropertiesChanged`. Refetch `GetAll` on those signals, or icons go stale.
- [x11] `AudioService::changed` also fires `AudioKind::nodes` after every volume change. Match each kind explicitly, never with a sink-or-else fallback.
- [x11] Disable cairo MIT-SHM on a probe surface right after connecting. Surfaces copy the flag at creation, and its pool stays resident.
- [x11] Disable cairo SHM with version `-1, -1`. Cairo checks for negative versions, so `0, 0` leaves SHM on.
- [x11] Wrap decoded `stb_image` pixels in the cairo surface in place. Copying doubled the transient peak of large images.
- [x11] Decode large images off the main thread. A full `4K` decode takes about half a second.
- [x11] Batch repaints from worker results through a timer, rescheduling only when none is pending. Rescheduling per arrival delays the paint.
- [x11] Paint overlapping translucent shapes with outer borders, then `CAIRO_OPERATOR_SOURCE` inner fills. `OVER` double-darkens the overlap.
- [x11] A nested `Xephyr` shows no cursor and ignores XTest clicks for the shell. Click inside its window by hand.
- [x11] A test that opens an X server nests in `Xephyr`. It skips cleanly when no server is available.
- [x11] Give managed windows a default size and `WM_DELETE_WINDOW`. i3 tiles them, and without the protocol its close kills the connection.
- [x11] Take a managed window's size from `ConfigureNotify`, not the request. Rebuild its backing store from that event before painting.
- [x11] Idle time is the server's `MIT-SCREEN-SAVER` clock. Keep a local reset time, or enabling idle management fires the screensaver instantly.
- [x11] Click-through windows use an empty `SHAPE` input region. Add only the boxes that must take clicks.
