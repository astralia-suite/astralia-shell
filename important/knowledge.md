# `astralia-shell` development knowledge

## Description

Hard-won rules from astralia-shell's development.
Can be updated if found new knowledge that supersedes old ones, or genuinely new ones.

## Rule

One statement + One explanation, ≤ 20 words each.
Drop an entry once newer knowledge fully supersedes it.
Entries describe the merged code base; name a symbol only while it exists.

## Entries

Entries without a tag apply to every backend. Tags `[wayland]` and `[x11]` mark backend-specific rules; where they disagree, the tag says which one applies.

## Shared: process and build

- Tests set `XDG_STATE_HOME` to a temporary directory first. `log` opens the real state file on first use and would fill it.
- A session-named path comes from `WAYLAND_DISPLAY`, else `DISPLAY`. Hyprland also exports `DISPLAY` for XWayland, so the order matters.
- Sway exports `I3SOCK` as well as `SWAYSOCK`. Check `SWAYSOCK` first, or a Sway session starts the X11 backend.
- Link `astralia-core-base` into the executable with `link_whole` and `export_dynamic`. Plugins resolve those symbols at `dlopen`.
- Plugins link `astralia-core-shared` statically with hidden visibility and `--gc-sections`. Each plugin keeps only what it uses.
- The plugins export only `astralia_backend_create`. Hidden visibility lets the linker drop unused functions.
- Never `dlclose` a backend. Its vtables and registered handlers outlive any point where closing would be safe.
- Look for a backend in `ASTRALIA_BACKEND_DIR`, then beside the executable, then the install directory. The build tree then runs without installing.
- A backend's static library links into its plugin with `link_whole` and `pic: true`. The same library links into its unit-test executable.
- Put each plugin's `shared_module` in the root `meson.build`, not in a subdirectory. A subdirectory puts the `.so` away from the loader's search path.
- Compile generated protocol `.c` files into the backend library only. The plugin takes the generated headers, or every interface is defined twice at link.
- Prefix a backend's headers with its directory when they share names with shared ones. A first-match include search would otherwise pick the backend copy.
- Keep a module's pure logic apart from EGL, GL and `xcb` code. The test binary then links no display library.
- The shell is zsh. `${var^}` and unquoted word splitting fail in a heredoc loop; generate files from a script instead.
- `pkill -f` with a pattern also matches the shell running the command. Kill by the PID captured from `$!`.
- Never live-test lock-screen or input-grabbing code carelessly. A past test hung the keyboard and forced a reboot.
- A test must not start a second shell in the user's session. The Wayland smoke test only checks that the plugin loads and fails cleanly.
- Test the bar's three styles through a scratch `XDG_CONFIG_HOME`. The real config stays untouched and the style shows in a screenshot.
- `grim` captures the whole screen, private windows included. Delete each screenshot right after viewing it.
- A generically named `constexpr` can collide with an identical name in an unrelated header. Modules that never include each other can still meet transitively.

## Shared: design

- Draw each module once over `ui::Canvas` and add a hook for backend-only art. Per-backend copies drift apart within a release.
- Hooks carry backend-only art: `LogoutLogoPainter`, `OverviewTileArt`, `SettingsArt`. Shared views then never include a render header.
- Share a module's logic only where both backends' formulas and constants match. `notification`, `settings` and bar styles differ in design, so they stay per backend.
- Keep constants only one host draws with in `<module>_style_config.h`. Constants a shared view draws with go in the module's shared config.
- Move call sites to the shared names instead of keeping alias headers. Aliases hide which service a file uses.
- Only Wayland animates; X11 calls `animation_set_instant(true)`. Models then animate unconditionally and snap to the end value on X11.
- Submenu icons are the `SubmenuIcon` enum in the shared model. Each backend maps it to its glyph.
- A canvas draws images at their natural size. Scale to the slot yourself, or a PNG icon covers the whole bar.
- `Canvas::gauge` is a no-op on cairo. The resource panel is Wayland-only, so the X11 canvas never needs an arc.
- Guard a service-signal slot with the content's alive flag. `Signal` has no disconnect, so a destroyed panel would be called.
- One overlay surface per monitor hosts whichever bar panel is open. Nine surfaces cost memory and input regions for panels shown one at a time.
- A panel's tray menu is a real popup: `xdg_popup` on Wayland, an override-redirect window on X11. Both need the parent's grab and position.
- The bar's `top_margin` and `side_margin` come from `BarStyleSpec`, never from a global constant. Each style attaches differently.
- `bar_style_resolve` maps `islands` to `continuous` on X11 only. `islands` is not built there, so the fallback stays out of shared code.
- A bar style needs its row in every `BarStyleSpec` table. A too-short table silently zero-fills.
- Panel fills follow `hl`, except the `i3` rows for network, Bluetooth, tray cells and the hovered tray entry. The `text_alpha06` and `text_alpha08` fills are deliberate.
- Removing a UI feature's draw code leaves its click kinds, state and handlers reading as live. Delete them in the same change.
- A refactor changing a shared function's contract must migrate every call site. Stragglers silently skip the new behaviour.
- A module's `Module` adapter lives in its own file behind a `make_*_module` factory. Cross-module needs arrive as hooks injected by the registry.
- Cross-module orchestration, such as the verb table and key dispatch, lives in `src/app/`. A module cannot include another module and `main.cpp` names none.
- A feature one backend lacks is a `Capabilities` flag. Hosts and verbs register only for reported flags.

## Shared: threads, D-Bus and processes

- Never read D-Bus properties synchronously on the reactor thread. Use `dbus_get_all_async`; a stalled BlueZ once froze the whole shell.
- No callback on the reactor thread may make a blocking D-Bus call. A synchronous Bluetooth read froze the shell 18 s and a polkit write 24 s.
- Building textures or rasterizing text from a D-Bus callback also blocks the loop. Build lazily in the paint path.
- Hold an async reply as an `sdbus::Slot` member. Destroying the owner then cancels the reply instead of calling a dead `this`.
- Coalesce D-Bus-signal refreshes to one call in flight plus a repeat flag. A signal burst otherwise queues one full refresh each.
- Never destroy an sdbus proxy inside its own async reply callback. Prune per-device proxies from the signal-match handler instead.
- Hold one sdbus proxy per fixed object, but keep changing NetworkManager paths one-off. Caching per-reconnect paths grows without bound.
- A local proxy destroyed right after firing an async call corrupts the connection it borrowed. Cache one proxy per object path.
- `SystemBus` polls both the bus fd and its `eventFd`. Sync calls queue signals internally, and only `eventFd` wakes the loop.
- Network scans run only while the panel is watching. An always-on seven-second `nmcli` rescan cost a process spawn each time.
- Never rescan on every NetworkManager state change. Forced rescans delay association and emit more churn; debounce through `schedule_rescan`.
- A timer- or signal-driven service must emit only on a real state diff. A once-a-second tick repainted every surface unconditionally.
- Keep earliest-deadline logic out of list order. Notification timeouts differ per entry, so the timer must take the minimum deadline.
- A config file the shell writes must be compared after parse before emitting. The shell's own atomic write trips its `inotify` watch.
- Watch a config file's directory with `inotify`, filtered by name. Editors save by rename, which drops a watch on the file.
- Compare only wallpaper-relevant config before repainting. Every `SettingsService::changed` would otherwise decode the wallpaper again.
- A file writer must create its own target directory. A fresh install otherwise fails the first save silently.
- Renaming a persisted config value needs a load-time mapping. Users otherwise silently lose the setting.
- `~` is a display convention, never a real path. `std::filesystem` never expands it; `core/path_home.h` collapses at UI edges and expands before use.
- `Config` path defaults must already be absolute. A default returned unexpanded is scanned directly by the wallpaper picker.
- Never allocate in a forked child before `exec`. `fork()` may copy a lock another thread held.
- Spawn with `posix_spawn`, not `fork`, on the reactor thread. glibc takes allocator locks other threads hold.
- Ignoring `SIGCHLD` and calling `waitpid` cannot coexist. Ignoring is process-wide and lets the kernel reap, breaking `waitpid` everywhere.
- Fire-and-forget processes double-fork instead of relying on global reaping. The grandchild reparents to init and is reaped there.
- Spawned children need an empty signal mask and default `SIGPIPE`. The shell blocks and ignores signals, and `exec` inherits both.
- A blocking `waitpid` thread beats watching a child's pipe in the poll loop. `poll()` once missed readiness for tens of seconds.
- A cancelled background process must be killed, not detached. Retyped searches otherwise pile up competing processes.
- `AsyncProcess` ignores a cancelled run by generation. A caller may restart at once, without waiting for the old child to die.
- Reusing a handle across restarts needs a generation counter. A cancelled worker can wake later with a stale result.
- Track "request in flight" with your own flag. The worker resets `pid` to `-1` the instant it marks the run done.
- Signal every worker's stop flag before joining any. Signalling and joining one at a time made `kill` block serially.
- Tear a module's background thread down in its destructor, not only its close path. A joinable thread's destruction calls `std::terminate`.
- A detached reader thread must own what it touches through a `shared_ptr`. The owner may be destroyed before the child exits.
- Never destroy the Wayland state at shutdown. Detached threads keep references into it and abort the process when it is freed.
- The process is multithreaded without spawning threads. Mesa, PipeWire and Pango each start their own.
- Capture a value synchronously at the action site. Deferring to the next repaint left panels opening at position zero.
- Capture click coordinates with the click event itself. The live shared pointer position may belong to another monitor.
- Never score an async result against a live mutable field. Freeze the input into its own field at start.
- A reactive `*_changed` flag must be set on every path that changes the value. One mutator forgetting it breaks only that path.
- An optimistic local write can suppress the `*_changed` flag it should trigger. Raise the flag at the write.
- Wipe password buffers with `explicit_bzero` after responding or cancelling. `std::string::clear` leaves the bytes in the heap.
- A connect-to-socket liveness probe is unreliable against a leftover socket file. Use a `flock`-guarded lock file instead.
- Take the single-instance `flock` before `daemonize()`. The child inherits the lock, so it survives the parent's exit.
- The `kill` IPC client releases its fd instead of closing it. The kernel closes it at shell exit, so the client returns afterwards.
- `main` ignores `SIGPIPE` and installs the crash handler. `SIGPIPE` terminates without a core; the handler logs a backtrace.
- The crash handler also installs a `std::set_terminate` hook. `daemonize()` sends `stderr` to `/dev/null`, hiding the exception text.
- A `backtrace_symbols_fd` trace maps to source only with `addr2line` against the exact crashed binary. A rebuild invalidates the offsets.
- The poll loop must `continue` on `EINTR` and log any other `poll()` error. Breaking on `EINTR` exited the whole process.
- Never size an allocation from an on-disk header without checking the file size. A corrupt cache header made `new[]` throw on a worker.
- libpng and libjpeg report errors by `longjmp`, skipping destructors. Hold buffers in plain or `volatile` pointers freed in the `setjmp` branch.
- libjpeg's default `error_exit` calls `exit()`. Install a handler that jumps back and returns `nullptr`.
- Every bundled asset needs the installed-path-plus-dev-tree fallback. A bare relative path resolves against the daemon's working directory.
- Brightness is set through `brightnessctl`, never a direct `sysfs` write. The file is root-only without a `uaccess` rule.
- A dock icon resolves through a `.desktop` id or `StartupWMClass` to `Icon`. The raw window class collapses unresolved apps onto one placeholder.
- A bounded `stat()` search beats a subprocess over a handful of candidates. Icon lookup needs existence checks only.
- The launcher directory listing runs `fd` through `popen` on the reactor thread. It blocks briefly; going asynchronous needs the browse screen to wait.
- Intel iGPUs often lack a GPU `hwmon` yet expose `gt_act_freq_mhz`. `find_gpu_clock_sensor` falls back to scanning `/sys/class/drm`.
- A widget- or IPC-opened panel primes its polled telemetry at the open site. Otherwise its cards pop in one by one over seconds.
- An async result polled on a throttle lands a throttle period late. Poll every tick while a request runs.
- Reopening idle management or lowering its timeout must reset the activity clock. A stale `last_activity` fires the screensaver instantly.
- Binding a PipeWire node listener alone delivers no live parameter updates. Call `pw_node_subscribe_params()` explicitly.
- A client's callback confirming a write is not proof the state changed. Device-backed nodes need writes routed through the parent Device's Route.
- A mutex must cover the read side of a shared buffer. Ring-buffer reads outside the lock raced the PipeWire thread.
- A second writer to shared per-bin state should prompt an audit of every existing writer. One recomputed off-mutex and became a race.
- A glibc arena cap stops a decode burst inflating RSS for good. The core allocator helper pins arenas and the `mmap` threshold.

## Shared: compositors

- i3 exports `I3SOCK`, not `I3_SOCK`. The socket path also lives in the `I3_SOCKET_PATH` property on the X root window.
- Subscribe to i3 `workspace` and `window` events on a dedicated socket. A refresh per event replaces watching root properties.
- An i3 workspace exists only while it holds windows or is visible. The list is not a fixed ten, so X11 shows ten pills through `workspace_slots`.
- The i3 compositor numbers workspaces globally and drops unnumbered names. It ignores the `global` paging flag.
- Sway windows carry `app_id`, not `window_properties.class`. The i3 tree parser names native Wayland windows by it.
- Derive tiled rects from `layout` and `percent`, not `GET_TREE` rects. i3 skips rendering hidden workspaces, leaving them stale.
- Wait for the i3 `RUN_COMMAND` reply before querying again. Otherwise the tree read can precede the command's effect.
- Ignore focus loss briefly after an i3 command, then refocus. i3 hands focus to a client on workspace switches.
- Hyprland emits `createworkspacev2` and `activewindowv2` before `workspacev2`. Treat any structural event in a batch as a full refresh.
- Refresh on `activewindowv2` too. Focus changes bump every client's `focusHistoryID`, so UI ordered by it goes stale.
- Hyprland emits no event for a tiled reorder inside a workspace. Client re-reads are opt-in through `watch_client_order`.
- Dock and overview views set `watch_client_order` when shown and clear it when hidden. A permanent poll costs idle wakeups.
- Every Hyprland request on the poll thread needs a socket timeout, read as "no change". An empty reply parsed as no clients blanks the dock.
- Hyprland's classic `dispatch` strings are deprecated for Lua. `HyprlandCompositor::dispatch` takes a `hl.dsp.*` expression.
- `j/getoption <name>` works over the existing request socket. No need to shell out to `hyprctl`.
- A workspace grid spanning monitors passes `global=true` on every tile and focus call. Hyprland otherwise remaps ids onto the focused monitor's page.
- The bar's workspace pills carry the compositor's absolute workspace id. Switching from a pill must pass `global=true`.
- An overlay reading compositor state on open refreshes first. Event-driven state can be stale by the time it opens.
- Compositor state is not ready when the first monitor's surface is created. Sizing from it needs a later catch-up.

## Shared: images, fonts and text

- Tabler codepoints collide with Codicons in the Nerd Font. Name the `tabler-icons` Pango family explicitly for icons.
- Register app fonts before the first `Text` exists. Pango snapshots fonts on creation and misses ones added later.
- Writing literal Unicode escapes through tool calls is unsafe. The JSON layer can swap them for glyph bytes; verify with `od -c`.
- Verify icon names against the widget's default-state property. Guessed names shipped wrong icons.
- A glyphless icon codepoint fails silently as an empty texture. Check icon constants against the actual font; `icons.h` cannot grow without font tooling.
- Size text and icon textures from fixed font metrics, not ink extents. Ink sizing made the baseline jitter between strings.
- A per-glyph text field advances each cell by the font's fixed advance. Summing ink widths collapses narrow glyphs.
- Slide a text input's whole run on its origin `x`, not on each length change. Only centered dot rows move their origin.
- A glyph missing from the primary font shifts the whole line's baseline. Pango's fallback inflates ascent; use the em dash over `U+00B7`.
- `show_layout`'s current point is the top-left corner, not the baseline. Adding ascent doubled the offset.
- Cairo output is premultiplied alpha but the blend convention is straight alpha. Mixing them squares alpha at antialiased edges.
- A `Texture`'s size is device pixels (`logical * scale`). Divide by `scale` when laying out, or HiDPI blocks come out 2x.
- A decode resolution must be device pixels, not logical. Decoding at logical size upscaled the lock avatar blurrily on scale 2.
- A pre-upload downsample derives its size from the source aspect ratio. Squashing to the box bakes in a stretch no crop undoes.
- `load_image_decode` renders SVGs into a square viewport only. Aspect-correct sprites need a direct `librsvg` render into a sized surface.
- A JPEG decode given a target size uses libjpeg `scale_denom`. Reduced-size decoding skips most pixels and memory.
- Font hinting differs between icons and text. `HINT_STYLE_NONE` keeps Tabler strokes thick while hinted text stays crisp.

## Wayland: rendering

- [wayland] The shell runs one OpenGL ES 2.0 context everywhere and shaders are `#version 100`. Both context creation sites must change together.
- [wayland] Clip with scissor rects plus a corner inset, not a stencil buffer. Correct while nothing visually touches a rounded edge.
- [wayland] A scissor clip helper needs a stack once clips nest. An unconditional disable on destruction wiped the outer clip.
- [wayland] Floor and ceil each scissor edge independently. Flooring the origin then rounding the size can drop the last row.
- [wayland] A container animating its own size clips children to the current size. Opacity alone lets oversized content draw mid-tween.
- [wayland] Gate that reveal clip on the height tween, not `animations.hasActive()`. A looping marquee kept it true forever.
- [wayland] A cached geometry value derives from the variable used for drawing. The animation target stored a wrong position.
- [wayland] A filled widget at the same origin as an earlier label paints over it. Offset the fill or draw the label last.
- [wayland] Overlapping translucent shapes of one colour double-darken. `Okinami` draws its rail and islands with opaque `palette::base`.
- [wayland] A multi-shape outline is two stacked layers: the whole silhouette in the border colour, the inset one in the fill.
- [wayland] A rounded rect placed above the surface edge shows only its bottom corners rounded. `Okinami` islands therefore need no shader.
- [wayland] `draw_texture_rect` rounds its `x` and `y`, but shapes do not. Round the shared edge first or a texture lands a pixel off.
- [wayland] Fractional texture positions blur every glyph. `GL_LINEAR` blends edge texels at half-pixel offsets; round before drawing.
- [wayland] `set_opacity()` is one global value per frame. Per-element alpha means baking it into each element's colour.
- [wayland] Node colour pointers are read at draw time. A temporary's address renders garbage once the stack slot is reused.
- [wayland] Variable-count colours come from a frame-scoped `static thread_local std::deque<Color>`. A deque keeps pointers valid across `push_back`.
- [wayland] `Node` and `Scene` have no `z`; claim order is paint order. Claim an overlay after every node it sits above.
- [wayland] `node_add_texture` draws at native size. An aspect-preserving decode in a fixed cell spills into neighbours without cropping.
- [wayland] A rebuilt-every-frame node tree pools its nodes. Reallocating at animation rate grows the heap high-water mark.
- [wayland] A narrower anti-aliasing band crisps rounded rect edges. One pixel fixed what a two-pixel `smoothstep` softened.
- [wayland] `rrect.frag`'s distance needs the interior `min(max(q.x, q.y), 0.0)` term. Otherwise a border wider than the radius fills the rect.
- [wayland] `draw_rounded_rect` always reads its border colour, even at zero width. Passing `nullptr` is a null read.
- [wayland] A `border_width` ported 1:1 from QML looks thinner here. Match `metrics::border_thin` (`2.0f`).
- [wayland] A concave hug corner is the same quarter-circle cutout as a convex fillet. `fillet_rgba` serves both.
- [wayland] A single shared `Renderer` resets itself in `begin_frame()`. Callers leaked stale opacity into the next paint.
- [wayland] Two hand-rolled opacity pipelines for one window drift apart. Unify on the shared Renderer and Scene path.
- [wayland] A fresh share context starts with `GL_BLEND` disabled. Raw GL paths enable it once per context.
- [wayland] `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` squares alpha over a transparent-cleared surface. Use `glBlendFuncSeparate` with `GL_ONE` for alpha.
- [wayland] `smoothstep` needs `edge0 < edge1`. Mesa tolerates reversed edges, NVIDIA may not; invert with `1.0 - smoothstep(lo, hi, x)`.
- [wayland] Declare `precision highp float` unconditionally in fragment shaders. `mediump` is narrow on NVIDIA and overflowed `logout`'s thunder shader.
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
- [wayland] A custom multi-pass shader effect bypasses the scene graph. Give it its own programs and FBOs, called from the module's paint.
- [wayland] A single-pass module shader can ride `Renderer::draw_custom`. Reuse `renderer/quad.vert` and `quad_vbo_` instead of owning FBOs.
- [wayland] Create a static quad's buffer once with its program. Per-frame `glGenBuffers` is driver churn.
- [wayland] A per-frame multi-tap fullscreen pass at native resolution stalls the poll loop. Render it to a downsampled FBO.
- [wayland] Rasterize once and cache by key for any procedural drawing. Bucket continuous inputs such as percentages or entries grow unbounded.
- [wayland] The default EGL swap interval blocks the poll thread on NVIDIA. `gl_make_current` sets `eglSwapInterval(display, 0)` on every bound surface.
- [wayland] Frame callbacks already pace every surface, so a zero swap interval causes no tearing. Skip the frame on any `EGL_FALSE`.
- [wayland] Never drive GL for a surface the compositor stopped compositing. A covered wallpaper would otherwise paint into the lock surface.

## Wayland: animation and frames

- [wayland] `request_frame` defers a mapped surface's repaint to `frame_done`. Inline `eglSwapBuffers` from a D-Bus handler froze the shell.
- [wayland] A deferred commit needs a one-pixel `wl_surface_damage_buffer`. Hyprland skips scheduling a frame for an undamaged commit.
- [wayland] `frame_done` arms the next frame callback before calling `draw()`. The paint's own commit must carry the next request.
- [wayland] A closed full-screen overlay must no-op its `request_frame`, gated on `open`. Blanket fan-outs repaint idle surfaces.
- [wayland] A closing overlay requests its frame before its fade completes. `on_complete` flips `open`, and a trailing call then never arms.
- [wayland] A live-preview overlay paces re-arming to its capture interval, not the display refresh. The idle GPU cost drops with no visible change.
- [wayland] `redraw_all_monitors` pokes per-monitor modules only. An open overlay reacting to an event needs its own `request_frame` loop.
- [wayland] A paint that re-arms only while a tween is active needs every time-varying element tween-driven. A wall-clock bar froze otherwise.
- [wayland] `AnimationManager` advances only when `tick()` runs each frame. A panel that forgets freezes, `on_complete` callbacks included.
- [wayland] Repeated `tick()` calls per instant are safe. Progress comes from wall-clock time, not accumulated delta.
- [wayland] Two `animate()` calls sharing an owner id cancel each other. Give each animated property its own id.
- [wayland] An animation's `on_complete` that destroys its surface fires inside paint's `tick()`. Re-check validity after `tick()` or defer the destroy.
- [wayland] An unlock animation's `on_complete` defers teardown via `DeferredCall`. Freeing inline destroys the manager mid-iteration.
- [wayland] A global instant switch cannot reach a self-re-arming `on_complete` chain. It recurses forever, so `marquee_scroll` checks it itself.
- [wayland] An animated per-frame layout value needs a snap-on-first-value sentinel. Otherwise the first frame tweens from a stale default.
- [wayland] A tweened value must not derive from another mid-tween value. It re-targets every frame instead of converging.
- [wayland] `AnimationManager` has no delay primitive. Chain a no-op tween whose `on_complete` starts the real one.
- [wayland] A render-side tween must not own a dismiss a service sweep already owns. A tween-owned dismiss fired on add under instant mode.
- [wayland] A module with its own entrance animation skips the generic overlay fade. Layering both produces a visible double fade.
- [wayland] A per-element opacity tween must not run during a container fade. Two alpha ramps multiply into an unintended curve.
- [wayland] A fading, resizing surface tweens opacity only and snaps geometry at the endpoints. One tween for both rescaled the content.
- [wayland] A panel's geometry locks at open. Recomputing height every frame desynced the close animation's start value.
- [wayland] `MarqueeTextState::marqueeing` means scrolling, not overflowing. Clip on texture width over box width.
- [wayland] A cell-snapped rain column varies its fall rate by skipping ticks with a fractional accumulator. A fractional step desnaps glyphs from the grid.
- [wayland] Hide an animated image after the close fade, not at close start. Clearing frames mid-tween pops the image out.
- [wayland] `animated_image_draw`'s alpha must fade the ring and fill as well. Skipping them left a logo frame lingering.
- [wayland] An `AnimatedImage` binds to one source; use one instance per source. Re-pointing leaves a stale job that never restarts.
- [wayland] `animated_image_animating` excludes single-frame stills. A static logo otherwise repaints overlays every frame.
- [wayland] Free an animated image's frame textures while its surface is off-screen. `hide` clears them and `show` reloads from the cache.

## Wayland: media and decode

- [wayland] One `libav` plugin, `media_plugin`, decodes every animated surface. `media_service` owns the cache path, key, decode and existence check.
- [wayland] The `.rgba` cache key includes every decode-shaping parameter, such as `fit`. Otherwise old-shape frames are reused silently.
- [wayland] Decode frames into a `.tmp` directory and rename on success. A killed decode left a partial set that later runs trusted.
- [wayland] Remove the rename target first. `rename(2)` into a non-empty directory fails, and a stale `.png` cache blocked every promotion.
- [wayland] A bounded gif decode needs `fps * seconds` frames, not a fixed count. A 36-frame cap showed 2.4 s of a 7 s logo.
- [wayland] Pick the hardware decoder from a preference list of `AVHWDeviceType`s. Try `CUDA`, then `VAAPI`, then software.
- [wayland] Zero-copy texture import is vendor-specific. `VAAPI` uses `DMA-BUF` and `EGLImage` on Mesa; `CUDA` interop is separate.
- [wayland] `AVCodecContext::get_format` takes a plain function pointer. Pass the wanted hardware format through `opaque`.
- [wayland] Build the decode filter graph from the first decoded frame. The hardware transfer format varies by driver.
- [wayland] Loop in-process video with a seek and flush. `av_seek_frame` plus `avcodec_flush_buffers` on EOF replaces the CLI loop flag.
- [wayland] A decoder can hold a frame back until the next packet or flush. Send a `nullptr` packet and drain before flushing.
- [wayland] Pace playback by each frame's `pts` against a wall-clock anchor. A fixed sleep per frame throttles decode, not display.
- [wayland] The zero-copy `VAAPI` path skips the filter graph. An `fps=` filter there changes nothing.
- [wayland] A thumbnail decode scales inside the filter graph. `decode_first_frame` emits `scale=W:H,format=rgba`, so frames are KB, not MB.
- [wayland] First-frame decodes pin `thread_count = 1`. Frame threading buys nothing and multiplies arenas; only streaming decode threads.
- [wayland] An animated wallpaper needs no pre-copy into the cache. `media_plugin` probes the source path by content.
- [wayland] Do not pre-transcode a video to scale or cap fps. The live filter graph does it, and software `libx264` caused a CPU spike.
- [wayland] One detached decode thread per picker tile bloats RSS. About 35 concurrent decodes kept eight 64 MB arenas.
- [wayland] `count_rgba_frames` counting one extension does not prove a directory is absent. A stale `.png` directory blocked the rename.
- [wayland] A sync-from-config function uploading only non-empty paths must clear the texture on an empty path. Reset it and bump the generation.
- [wayland] Clearing a texture in memory does not repaint. Both branches call `wallpaper_request_frame`, or the old wallpaper stays.
- [wayland] Wallpaper decode closures capture a `std::weak_ptr<int>` lifetime token. Per-monitor state can be freed on hotplug before the closure runs.
- [wayland] The backend pins `M_ARENA_MAX` to 2 and `M_MMAP_THRESHOLD` and `M_TRIM_THRESHOLD` to 256 KB. Dynamic thresholds otherwise leave a worker arena holding 47 MB.

## Wayland: visualizer and effects

- [wayland] Run the visualizer window on its own thread with a share-context `EGLContext`. A GPU-heavy overlay that still stalls the loop must leave it.
- [wayland] A render thread and the main thread never hold one `EGLSurface` current together. The render thread owns it while open.
- [wayland] A render-thread module drops the main thread's `EGLSurface` via `rest_egl_current`. Skipping it lost `eglMakeCurrent` with `EGL_BAD_ACCESS`.
- [wayland] The shared context rests on `egl_rest_surface`, never a module's surface. It is surfaceless or a 1x1 pbuffer; resting on the first bar broke on unplug.
- [wayland] A share context uses the main context's exact `egl_context_attribs`. Different reset-notification attributes fail with `EGL_BAD_MATCH`.
- [wayland] `gl_make_current` consumes the real EGL error. A later `eglGetError` misleadingly reads `EGL_SUCCESS`.
- [wayland] Do not drive a render-thread window from the poll-thread `FrameClock`. On Mesa it deadlocked the frame pump after one frame.
- [wayland] The visualizer presents every frame and starts its fade on the first presented one. Budget gating pinned the fade at zero.
- [wayland] `visualizer/fullscreen.vert` emits only `gl_Position`. A fragment shader paired with it uses `gl_FragCoord`, not a `vUv` varying.
- [wayland] The sphere's accumulator must be a `GL_FLOAT` target. Hundreds of splats per pixel clamp at `1.0` in `UNORM`.
- [wayland] `ncs`'s `sphere.radius` is an exclusion-disc radius, not the blob's size. Too small shows a rectangle around a punched hole.
- [wayland] Keep every `ncs.glsl` default constant verbatim. The shader is a tuned whole and approximations read wrong.
- [wayland] The visualizer canvas stays square. Otherwise `sphereCoords()` yields an ellipsoid, as blob geometry is pixel-absolute.
- [wayland] `ncs`'s `time` uniform is a frame counter paced to `fps`. Its decay constants are per-frame, so an unpaced port over-reacts.
- [wayland] The shell has no runtime shader preprocessor. Flatten ported multi-file shaders at authoring time into `assets/shaders/`.
- [wayland] A larger `arc_gauge` scales its stroke by the reference stroke-to-diameter ratio. `kLockResGaugeStrokeRatio` stops a hairline.
- [wayland] Matrix and visualizer tile as regular windows. A `float = true` rule is a rejected direction, not an omission.

## Wayland: surfaces and protocol

- [wayland] Wayland cannot tell which output the pointer is over. Track a best-effort hint from your own surfaces' enter and motion events.
- [wayland] Clear that pointer hint when its output is removed. A stale pointer to a freed `MonitorOutput` caused a use-after-free.
- [wayland] Optional protocol events need sane fallbacks, not zero. Some compositors never send `repeat_info`, and `0/0` disables key repeat.
- [wayland] A coordinate from one layer-shell surface is invalid on another without translating margins. Origins differ.
- [wayland] Hover-driven surface changes touch size only, never margin or exclusive zone. A margin change repositioned mid-hover and looped.
- [wayland] `exclusive_zone` excludes the same-edge margin. The compositor adds it already, so including it reserves the space twice.
- [wayland] A bind's reply is a second round-trip. Do one extra roundtrip before reading output state.
- [wayland] A live-update IPC event may lack a field only a full snapshot has. Check whether an adjacent event carries and can cache it.
- [wayland] Start the polkit agent only after keyboard input and a GLib loop exist. An agent that cannot prompt fails every `pkexec`.
- [wayland] A struct member cannot share a name with a protocol type in the same header. `xdg_surface *xdg_surface` breaks lookup downstream.
- [wayland] Create a real `xdg_toplevel` on open and destroy it on close. A mapped transparent toplevel still shows in switchers.
- [wayland] `ToplevelWindowBase` has no resize callback. A module with a per-size buffer compares live width and height each paint.
- [wayland] Reset a torn-down window's `FrameClock` fields as well as its handles. A stale non-null callback silences `request_frame` forever.
- [wayland] Release a pending `wl_callback` with `wl_callback_destroy`, never by nulling. `wl_surface_destroy` does not free it.
- [wayland] Release an `EGLSurface` from the current context before `wl_egl_window_destroy`. A current surface defers teardown into freed memory.
- [wayland] Every `EGLSurface` teardown calls `gl_release_if_current` first. It unbinds only that surface, keeping the context bound surfacelessly.
- [wayland] A bottom-anchored layer surface changes size and buffer in one commit. Sway repositions at once; a separate commit dropped hover.
- [wayland] Keep layer overlays mapped across same-output toggles. Recreating a same-namespace surface left it uncomposited or unrouted on Hyprland.
- [wayland] A hand-rolled toggle can miss a shared lifecycle fix. `launcher_toggle` hit the uncomposited-surface bug independently.
- [wayland] A destroy-on-close toplevel can reuse an earlier window's address. State keyed by address must clear when the window goes.
- [wayland] A mostly click-through layer surface takes clicks through a `wl_region` union of sub-rects. Notifications rebuild theirs each paint.
- [wayland] A `PerMonitorModule` pointer move fires on every monitor with the same coordinates. Guard hover on `pointer.focused_surface` being its own.
- [wayland] The pointing-hand cursor needs `wants_pointing_hand_cursor()` on both `Module` and `PerMonitorModule`. Otherwise bars keep the arrow.
- [wayland] Every panel with exclusive keyboard interactivity needs its own key-dispatch arm. Nothing enforces the two stay in sync.
- [wayland] A modifier-aware shortcut matches `KeyEvent::base_sym`, not `text`. `Shift`+`1` is `!` in `text`.
- [wayland] A hover highlight clears on lost surface focus, not only on motion. Stolen focus leaves a stale hovered index.
- [wayland] A highlight teardown nulls its source indices before recomputing. `logout`'s close re-lit the button through the exit.
- [wayland] A panel's staged dismissal is coded identically on every path. `Escape` and an outside click once disagreed.
- [wayland] A generic "click missed" guard excluding a sibling surface pushes the decision onto it. That handler must know every overlay above.
- [wayland] An overlay opened from a bar widget needs its own `*_here` term in `bar_paint`'s `want_shown`. Opening it steals focus and autohide collapses the bar.
- [wayland] A single-instance overlay bound to one output is open only there. Gate on `bound_output() == mon.output.wl`.
- [wayland] A per-monitor `apply_config` reads its `new_cfg`, never `app.cfg`. The latter is assigned after the fan-out.
- [wayland] A per-monitor `create_surface()` must not gate on config state. It runs once, so a startup check broke toggle-on.
- [wayland] Every per-monitor `destroy()` drops its pending frame callback. A late `frame_done` otherwise runs on freed state.
- [wayland] A per-monitor layer surface's `.closed` handler is a no-op. `registry_global_remove` owns cleanup; exiting there kills the whole shell.
- [wayland] `registry_global_remove` tells overlays an output is gone via `Module::on_output_removed`. Otherwise they keep a dangling `bound_output`.
- [wayland] A per-monitor text-input client clears its own focus in `destroy()`. `PerMonitorModule::destroy()` bypasses `on_output_removed`.
- [wayland] An `xdg_popup` needs the parent layer surface, an anchor rectangle and the triggering input serial. Without the serial the compositor denies the grab.
- [wayland] A popup's `done` event dismisses it. Destroy the popup and notify the panel, since the compositor has already dropped the grab.
- [wayland] `ext-session-lock` withholds `locked` until every output presents a non-null buffer. Paint all lock surfaces before flushing.
- [wayland] `unlock_and_destroy` needs a `wl_display_roundtrip` before teardown. Otherwise the server may kill the client with a protocol error.
- [wayland] Ack a lock-surface `configure` before any commit and on every resend. Defer the `wl_egl_window` resize past the handler.
- [wayland] The startup overlay `init_egl` loop must not gate on `surface()`. The lock owns none until locked.
- [wayland] A `pam_start_confdir` service needs an `account` rule. Without it `pam_acct_mgmt` returns `PAM_PERM_DENIED` on a correct password.

## X11 backend

- [x11] Wall-clock timers use a `CLOCK_BOOTTIME` `timerfd`. Monotonic clocks pause in suspend, leaving the clock stale after resume.
- [x11] Never wipe the EWMH connection after `xcb_ewmh_init_atoms_replies` fails. Its failure path already frees everything, so wiping double-frees.
- [x11] Strings with embedded NULs, such as `WM_CLASS`, need the `sv` literal. A `std::string_view` from `const char*` stops at the first NUL.
- [x11] Under i3, dock windows ignore requested position and struts. Inset content inside a full-width ARGB dock instead.
- [x11] A 32-bit window needs its own colormap and a border pixel. Otherwise creation fails with `BadMatch` against the root depth.
- [x11] Round-trip before `xcb_disconnect`. It does not sync, so Xorg drops unprocessed requests and loses exit cleanup.
- [x11] Never set `ESETROOT_PMAP_ID` on the shell's own pixmap. Wallpaper setters kill its owner, which would disconnect the whole shell.
- [x11] The wallpaper destructor resets the root background and deletes `_XROOTPMAP_ID`. Otherwise the root keeps the image alive.
- [x11] Never `cairo_device_finish` a cairo-xcb device on the shell connection. The device is shared, so finishing it breaks the bar.
- [x11] RandR notify events carry no event window. Route them with `EventLoop::on_event`, since the root handler belongs to the workspace service.
- [x11] Let one `OutputService` own the RandR notify. `EventLoop::on_event` keeps a single handler per type, so a second owner replaces it.
- [x11] Drain the xcb queue before each `poll`, never inside a poll source's `prepare`. A handler may add a source and invalidate iteration.
- [x11] Take window geometry from `CompositorClient` positions minus the monitor's work area. `reserved` is `{left, top, right, bottom}`.
- [x11] Overlays take input focus, never an active keyboard grab. A grab blocks the window manager's hotkeys, so toggles never arrive. The lock is the one exception: it must hold input.
- [x11] The lock grabs with `owner_events` off on its first window and shows only that window with focus. Events then reach one handler, and hiding restores the previous focus.
- [x11] The lock refuses to show when the keyboard grab fails. A lock screen that does not hold input is worse than none.
- [x11] Read xkb modifiers from each key press's `state` field. Overlays opened by a Shift hotkey miss its release.
- [x11] Map a popup beside a panel with `show(false)`, never focus. Taking focus fires the panel's focus-out close; its `owner_events` grab still routes clicks.
- [x11] Hide bars for unplugged or disabled outputs, never destroy them. `EventLoop` cannot remove windows or timers, so they would dangle.
- [x11] Call `EventLoop::reschedule()` when an event moves a timer's deadline earlier. Deadlines are only recomputed after firing.
- [x11] Drive GLib through an `EventLoop` poll source. Polkit and GDBus use changing fds that fixed fd watches miss.
- [x11] SNI items signal `NewIcon` and `NewStatus`, rarely `PropertiesChanged`. Refetch `GetAll` on those signals, or icons go stale.
- [x11] `AudioService::changed` also fires `AudioKind::nodes` after every volume change. Match each kind explicitly, never with a sink-or-else fallback.
- [x11] Disable cairo MIT-SHM on a probe surface right after connecting. Surfaces copy the flag at creation, and its pool stays resident.
- [x11] Disable cairo SHM with version `-1, -1`. Cairo checks for negative versions, so `0, 0` leaves SHM on.
- [x11] Decode images with `stb_image` and `resvg`, not gdk-pixbuf. gdk-pixbuf pulls in sandboxed loaders, extra threads and megabytes of libraries.
- [x11] Wrap decoded `stb_image` pixels in the cairo surface in place. Copying doubled the transient peak of large wallpapers.
- [x11] Decode JPEGs with `libjpeg` scale denominators of 2, 4 or 8. Reduced-size decoding skips most pixels and memory.
- [x11] Call `malloc_trim(0)` after bursts like decodes or broad searches. glibc keeps freed small chunks, pinning several megabytes otherwise.
- [x11] Set `M_ARENA_MAX` to 1 and a fixed `M_MMAP_THRESHOLD`. A decode thread's private arena kept about 50 MB after the tab closed.
- [x11] Slow idle heap growth is glyph-cache warm-up plus fragmentation, not a leak. `heaptrack` showed live heap flat after three idle minutes.
- [x11] Decode wallpaper thumbnails off the main thread, through the cover cache. A full `4K` decode takes about half a second.
- [x11] Run the thumbnail worker at nice `10`. On the X201's two cores, decoding otherwise competes with the UI thread.
- [x11] Touch a cache file's mtime on every hit. Pruning treats mtime as last use, so wallpapers in use survive.
- [x11] Cache opaque covers as JPEG and transparent ones as PNG. JPEG writes faster and is far smaller on the X201's slow disk.
- [x11] Keep thumbnails as X-side surfaces made with `cairo_surface_create_similar` on first paint. Image surfaces upload to the server every paint.
- [x11] Batch repaints from worker results through a timer, rescheduling only when none is pending. Rescheduling per arrival delays the paint.
- [x11] Paint overlapping translucent shapes with outer borders, then `CAIRO_OPERATOR_SOURCE` inner fills. `OVER` double-darkens the overlap.
- [x11] A nested `Xephyr` shows no cursor and ignores XTest clicks for the shell. Click inside its window by hand.
- [x11] A test that opens an X server nests in `Xephyr`. It skips cleanly when no server is available.
