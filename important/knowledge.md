# `astralia-shell` development knowledge

## Description

Hard-won rules from astralia-shell's development.
Can be updated if found new knowledge that supersedes old ones, or genuinely new ones.

## Rule

One statement + One explanation, ≤ 20 words each.
Drop an entry once newer knowledge fully supersedes it.

## Entries

Entries without a tag apply to every backend. Tags `[wayland]` and `[x11]` mark backend-specific rules; where they disagree, the tag says which one applies.


- Tests set `XDG_STATE_HOME` to a temporary directory first. `log` opens the real state file on first use and would fill it.
- A session-named path comes from `WAYLAND_DISPLAY`, else `DISPLAY`. Hyprland also exports `DISPLAY` for XWayland, so the order matters.
- Never read D-Bus properties synchronously on the reactor thread. Use `dbus_get_all_async`; a stalled BlueZ once froze the whole shell.
- Hold an async reply as an `sdbus::Slot` member. Destroying the owner then cancels the reply instead of calling a dead `this`.
- Coalesce D-Bus-signal refreshes to one call in flight plus a repeat flag. A signal burst otherwise queues one full refresh each.
- Network scans run only while the panel is watching. `hl`'s always-on 7 s `nmcli` rescan cost a process spawn each time.
- Keep earliest-deadline logic out of list order. `notification` timeouts differ per entry, so the timer must take the minimum deadline.
- A config file the shell writes must be compared after parse before emitting. The shell's own atomic write trips its `inotify` watch.
- i3 exports `I3SOCK`, not `I3_SOCK`. The IPC socket path also lives in the `I3_SOCKET_PATH` property on the X root window.
- Subscribe to i3 `workspace` and `window` events on a dedicated socket. A refresh per event replaces watching X root properties.
- An i3 workspace exists only while it holds windows or is visible. The workspace list is not a fixed ten.
- Hyprland emits `createworkspacev2` and `activewindowv2` before `workspacev2` on a workspace switch. Treat any structural event in a batch as a full refresh.
- Hyprland client re-reads are opt-in through `watch_client_order`. A once-a-second `j/clients` poll costs idle wakeups nobody needs.
- Link `astralia-core-base` into the executable with `link_whole` and `export_dynamic`. Plugins resolve those symbols at `dlopen`.
- Never `dlclose` a backend. Its vtables and registered handlers outlive any point where closing would be safe.
- Look for a backend in `ASTRALIA_BACKEND_DIR`, then beside the executable, then the install directory. The build tree then runs without installing.
- The shell is zsh. `${var^}` and unquoted word splitting fail in a heredoc loop; generate files from a script instead.
- A backend's static library links into its plugin with `link_whole` and `pic: true`. The same library links into its unit-test executable.
- Put each plugin's `shared_module` in the root `meson.build`, not in a subdirectory. A subdirectory puts the `.so` away from the executable the loader checks.
- Drain the xcb queue before each `poll`, never inside a poll source's `prepare`. A handler may add a poll source and invalidate the iteration.
- X11 modules take window geometry from `CompositorClient` positions minus the monitor's work area. `reserved` is `{left, top, right, bottom}`.
- Compare only wallpaper-relevant config before repainting. Every `SettingsService::changed` would otherwise decode the wallpaper again.
- A test that opens an X server nests in `Xephyr`. It skips cleanly when no server is available.
- Plugins link `astralia-core-shared` statically with hidden visibility and `--gc-sections`. The executable stays small and each plugin keeps only what it uses.
- The Wayland and X11 plugins export only `astralia_backend_create`. Hidden visibility lets the linker drop unused functions.
- Draw each module once over `ui::Canvas` and add a hook for backend-only art. Per-backend copies drift apart within a release.
- Only Wayland animates; X11 calls `animation_set_instant(true)`. Models then animate unconditionally and snap to the end value on X11.
- A canvas draws images at their natural size. Scale to the slot yourself, or a PNG icon covers the whole bar.
- `Canvas::gauge` is a no-op on cairo. The resource panel is Wayland-only, so the X11 canvas never needs an arc.
- One overlay surface per monitor hosts whichever bar panel is open. Nine surfaces cost memory and input regions for panels shown one at a time.
- Guard a service-signal slot with the content's alive flag. `Signal` has no disconnect, so a destroyed panel would be called.
- A bar panel's tray menu is a dialog card under the tray card. A popup window needs per-backend grabs and positioning.
- X11 shows ten workspace pills from any output through `workspace_slots`. i3 lists only workspaces that exist.
- Sway exports `I3SOCK` as well as `SWAYSOCK`. Check `SWAYSOCK` first, or a Sway session starts the X11 backend.
- Sway windows carry `app_id`, not `window_properties.class`. The i3 tree parser names native Wayland windows by it.
- Test the bar's three styles through a scratch `XDG_CONFIG_HOME`. The real config stays untouched and the style shows in a screenshot.
- Prefix a backend's headers with its directory when they share names with shared ones. A first-match include search would otherwise hand shared headers the backend copy.
- Compile generated protocol `.c` files into the backend library only. The plugin takes the generated headers, or every interface is defined twice at link.
- Never destroy the Wayland state at shutdown. Detached `async_process` threads keep references into it and abort the process when it is freed.
- A test must not start a second shell in the user's session. The wayland smoke test only checks the plugin loads and fails cleanly without a display.
- `pkill -f` with a pattern also matches the shell running the command. Kill by the PID captured from `$!`.
- A callback `AsyncProcess` ignores a cancelled run by generation. A caller may restart at once, with no wait for the old child to die.
- Move call sites to the shared names instead of keeping alias headers. Aliases hide which service a file uses.
- Share a module's logic only where both backends' formulas and constants match. `notification`, `settings` and bar styles differ in design, so they stay per backend.
- Keep constants only one host draws with in `<module>_style_config.h`. Constants a shared view draws with go in the module's shared config.
- The launcher directory listing runs `fd` through `popen` on the reactor thread. It blocks briefly; making it asynchronous needs the browse screen to wait for results.
- The Wayland logout lock button runs `astralia lock`. The old `astralia-shell lock` name stops existing once the merged binary replaces the old install.
- Submenu icons are the `SubmenuIcon` enum in the shared model. Each backend maps it to its glyph, so shared code never includes a render header.

## Wayland backend (from `hl`)

### Wayland: 1. Subprocesses in a multithreaded process

- [wayland] **The poll loop must never block, even briefly.** One poll() loop drives every surface; a single blocking call freezes the whole shell.
- [wayland] **No code path on the shared poll thread may make a synchronous blocking D-Bus call, in any callback.** A blocking Bluetooth `getProperty()` chain froze the shell 18s; a polkit `setProperty` froze it 24s.
- [wayland] **Building GPU textures or rasterizing text from a D-Bus callback blocks the poll loop, not just network I/O.** `notification_apply_content`'s eager Pango/Cairo/GL work on every Bluetooth connect stuttered the shell; build lazily in the paint path.
- [wayland] **astralia-shell is multithreaded even though it spawned no threads itself.** Mesa, pipewire, and Pango each start their own background threads automatically.
- [wayland] **The mpris player scan was the last synchronous D-Bus path on the shared poll thread.** A Bluetooth connect's `NameOwnerChanged` burst ran its blocking `ListNames`/`Get` chain, freezing the shell.
- [wayland] **Never allocate memory in a forked child before `exec()`.** `fork()` may copy a lock another thread held.
- [wayland] **Ignoring SIGCHLD and calling waitpid() cannot coexist.** Ignoring SIGCHLD is process-wide and lets the kernel auto-reap, breaking waitpid() everywhere.
- [wayland] **Fire-and-forget processes should double-fork, not rely on global reaping.** The intermediate child exits immediately, reparenting the grandchild to init for automatic reaping.
- [wayland] **A blocking `waitpid` thread beats watching a child's fd in the poll loop.** `poll()` sometimes missed pipe readiness for tens of seconds.
- [wayland] **A cancelled background process must be killed, not just detached.** Leaving it running lets retyped searches pile up competing processes that never finish.
- [wayland] **SIGKILL stops future CPU use but doesn't guarantee immediate process death.** Confirm actual death before forking a replacement, or the two processes compete.
- [wayland] **Reusing a handle across restarts needs a generation counter.** A cancelled worker can still wake later with a stale result unless generations are checked.
- [wayland] **`async_process`'s worker thread resets `pid` to `-1` the instant it sets `done` and fills `buffer`.** Track "request in flight" with your own bool flag, not by reading `pid` back later.
- [wayland] **Stopping N worker threads on shutdown must signal every stop flag before joining any.** Signal-then-join one at a time made `astralia-shell kill` block on each column's thread serially.
- [wayland] **A module's owned background thread must be torn down in its destructor, not only its explicit-close path.** `astralia-shell kill` skipped the visualizer's shutdown; the joinable thread's destruction called `std::terminate()` mid-`eglSwapBuffers`.
- [wayland] **`fork()` on the poll thread can stall it.** glibc takes allocator locks other threads hold; use `posix_spawn`.
- [wayland] **`request_frame` defers a mapped surface's repaint to `frame_done`, never paints inline.** Inline `eglSwapBuffers` from a D-Bus handler froze the shell; only the first unmapped paint stays synchronous.
- [wayland] **The deferred `request_frame` commit needs a 1px `wl_surface_damage_buffer` to guarantee a `frame_done`.** Hyprland skips scheduling a frame for a bufferless, undamaged commit, so the callback never fires.
- [wayland] **`frame_done` must arm the next frame callback before calling `draw()`.** Arming only outside halved animation rate; the paint's own commit must carry the next request.
- [wayland] **A timer- or signal-driven service must gate its redraw callback on a real state diff.** `bluetooth_tick` repainted every surface once per second unconditionally; compare state and set a `dirty` flag.
- [wayland] **Reacting to every NetworkManager `State`/`ActiveConnections` change with `nmcli --rescan yes` self-amplifies.** Forced rescans delay association and emit more state churn; debounce through `schedule_rescan`.

### Wayland: 2. Rendering

- [wayland] **Don't render the Tabler icon font via fontconfig plus Pango.** Late-registered app fonts aren't reliably picked up by Pango's font map; use FreeType+Cairo directly.
- [wayland] **astralia-shell clips using scissor rects plus a corner inset, not a stencil buffer.** Correct as long as nothing needs to visually touch a rounded edge.
- [wayland] **A scissor clip helper needs a stack, not one slot, once clips nest.** An unconditional glDisable on destruction wiped the outer clip when clips nested.
- [wayland] **A container animating its own size must clip children to the current size.** Fading opacity alone doesn't stop oversized content rendering outside the container mid-tween.
- [wayland] **Gate that reveal clip on the height tween, not `animations.hasActive()`.** A looping marquee kept `hasActive()` true forever, clipping the network panel's sub-dialog off-screen permanently.
- [wayland] **A cached geometry value must derive from the same variable used for drawing.** Using the animation's target width instead of the current frame stored a wrong position.
- [wayland] **Scissor rects should floor/ceil each edge independently, not truncate uniformly.** Flooring the origin then rounding the size can drop the last row or column.
- [wayland] **A narrower anti-aliasing band makes rounded-rect edges crisper.** A 2px smoothstep band softened straight edges; 1px fixed it.
- [wayland] **A filled widget drawn at the same origin as an earlier label silently paints over it.** Two settings-tab tiles once started at the label's own `(x,y)`, hiding it under the first tile.
- [wayland] **Two overlapping translucent shapes of one colour double-darken where they overlap.** `Okinami`'s rail and islands use opaque `palette::base` so the overlap is invisible.
- [wayland] **`draw_texture_rect` rounds its `x`/`y`, but rects and rounded rects don't.** A texture abutting a shape at a half-pixel edge lands a pixel off; `std::round` the shared edge first.
- [wayland] **A multi-shape outline is two stacked layers: whole silhouette in the border colour, inset silhouette in the fill.** Per-shape borders cross the joins.
- [wayland] **A rounded rect placed above the surface edge (negative `y`) shows only its bottom corners rounded.** The surface clips the rest, so `Okinami`'s islands need no shader.
- [wayland] **Drawing textures at fractional pixel positions blurs every glyph and icon.** GL_LINEAR sampling blends edge texels 50/50 at .5px offsets; round positions before drawing.
- [wayland] **`show_layout`'s current point is the top-left corner, not the baseline.** Adding ascent on top of that doubled the offset, rendering text clipped near the bottom.
- [wayland] **`set_opacity()` is a single global value per frame, not per-node.** Simultaneous different opacities need baking alpha into each element's own color instead.
- [wayland] **A bounded `stat()` search beats a subprocess search over a handful of candidates.** Icon resolution only needs existence checks against known paths, not an open-ended directory search.
- [wayland] **Writing literal Unicode escapes through tool calls is unsafe.** The JSON layer can silently replace them with actual glyph bytes; verify with `od -c`.
- [wayland] **Verify icon names against the widget's actual default-state property, not a plausible name.** Several icons were wrong because they were guessed from a config property list.
- [wayland] **A glyphless icon codepoint fails silently as an empty texture, caught only by `rasterize_icon`'s `klog`.** `volume_empty` shipped as `U+0001`; check icon constants against the actual font.
- [wayland] **Size text/icon textures from fixed font metrics, not per-string ink extents.** Ink-based sizing made baseline position jitter as string content changed between renders.
- [wayland] **A per-glyph text-field draw must advance each cell by the font's fixed `Pango` advance, not ink width.** Summing ink widths drops side bearings, collapsing narrow glyphs so input reads shorter than normal.
- [wayland] **A text input's whole-run slide belongs on its origin `x`, not every length change.** Left-aligned fields keep a fixed origin; only centered dot rows whose origin moves should animate.
- [wayland] **Cairo output is premultiplied alpha, but astralia-shell's blend convention is straight alpha.** Uploading one as the other silently squares alpha at edges, washing out antialiased pixels.
- [wayland] **`draw_rounded_rect` always reads its border-color argument, even at zero border width.** Passing nullptr for "no border" is a null-pointer read, not a no-op.
- [wayland] **A rebuilt-every-frame node tree should pool and reuse nodes, not reallocate.** Reallocating at animation frame rate causes unbounded heap high-water-mark growth over time.
- [wayland] **A refactor changing a shared function's contract must migrate every call site.** Leaving old `add_child()` around let stragglers silently skip rendering after the pooling refactor.
- [wayland] **An animated per-frame layout value needs a snap-on-first-value sentinel before tweening.** Without one, the first frame animates from a stale or zero default.
- [wayland] **A tweened value must not be computed from another value that's itself mid-tween.** Otherwise it re-targets every frame instead of converging on a moving anchor.
- [wayland] **AnimationManager has no delay primitive, so chain a no-op tween to get one.** `animate()` starts immediately; a dummy tween's `on_complete` triggers the real one.
- [wayland] **A render-side visual tween must not own the authoritative dismiss trigger when a service-side sweep already does.** `NotificationService::timer_tick` already dismisses on wall-clock; a tween-owned dismiss fired on add under `animation_set_instant`.
- [wayland] **A module with its own entrance animation should skip the generic overlay-panel fade.** Layering both fades produces a visible double-fade the reference lacks.
- [wayland] **A pre-upload downsample must derive its target size from the source's aspect ratio.** Squashing to the destination box's raw dimensions bakes in a stretch a later crop can't undo.
- [wayland] **A per-element opacity tween must not run while a container-level fade is active.** Two independently-timed alpha ramps multiply into a visibly different, non-obvious result.
- [wayland] **Node color pointers are read at draw time, not when stored, so must outlive the frame.** Passing a temporary `Color`'s address renders garbage once the stack slot is reused.
- [wayland] **`node_add_texture` draws at native pixel size, not scaled to its container.** An aspect-preserving decode in a fixed cell without cropping spills into neighbors.
- [wayland] **A `smoothstep(edge0, edge1, x)` call needs `edge0 < edge1`; reversed or equal edges are spec-undefined.** Invert with `1.0 - smoothstep(lo, hi, x)`; floor a computed edge that can reach `0`.
- [wayland] **Mesa silently tolerates reversed or equal `smoothstep` edges; NVIDIA may not.** Always order edges before calling `smoothstep`, don't rely on driver leniency.
- [wayland] **Declare `precision highp float` unconditionally in fragment shaders touching pixel-scale magnitudes; `mediump` is real (narrow) on NVIDIA, fake on Mesa.** `logout`'s thunder shader overflowed mediump on NVIDIA, rendering a filled rectangle; `#ifdef GL_FRAGMENT_PRECISION_HIGH` isn't reliably defined there.
- [wayland] **A `float(x == x)` NaN guard must run before `clamp`, not after.** `clamp` launders `NaN` to an endpoint, so guard the raw value first: `f *= float(f == f)`.
- [wayland] **`pow(0.0, 0.0)` is undefined: NVIDIA returns `1.0`, Mesa Intel `NaN`.** Guard every `pow` base with `max(base, 1e-4)`.
- [wayland] **The rasterize-once-cache-by-key pattern generalizes beyond text/icons to any procedural drawing.** Cache keys must bucket continuous inputs like percentages, or entries grow unbounded.
- [wayland] **A custom multi-pass GPU shader effect bypasses the Node/Scene graph entirely.** Give it its own programs/FBOs, called directly from the module's paint function.
- [wayland] **The shell runs one OpenGL ES 2.0 context everywhere, shaders are `#version 100`.** Two creation sites, `wayland_registry.cpp` and `visualizer.cpp`'s render-thread share context, must change in lockstep.
- [wayland] **GLSL ES 1.00 requires a compile-time-constant loop bound; a uniform-derived one doesn't compile.** Use a fixed `MAX_*` cap with an early `break`, sized past the config's live range.
- [wayland] **GLSL ES 1.00 has no `matNxM` typenames (`mat3x3`, `mat4x4`), only `mat2`/`mat3`/`mat4`.** `common.glsl` once used `mat3x3` and silently broke every sphere-pipeline program, since all three concatenate it.
- [wayland] **GLSL ES 1.00 has no array constructors (`float[8](...)`) or whole-array assignment.** Assign per element after a bare declaration, as `sphere1_vert_main.glsl`'s `setAudio()` does.
- [wayland] **`GL_UNPACK_ROW_LENGTH` needs the `GL_EXT_unpack_subimage` extension-string probe on ES 2.0; it isn't a core token.** `texture_row_length_supported()` gates on `glGetString(GL_EXTENSIONS)` again, not a hardcoded `true`.
- [wayland] **`GL_RED`/`GL_MAX` (blend equation) aren't core ES 2.0 tokens; use the `_EXT` suffix (`GL_RED_EXT`, `GL_MAX_EXT`) from `GL_EXT_texture_rg`/`GL_EXT_blend_minmax`.** Both extensions are effectively universal, unlike norm16, so no separate support probe.
- [wayland] **A file using `GLES2/gl2ext.h` symbols must include it after `GLES2/gl2.h` (via `render/gl.h`), not before.** `gl2ext.h` uses `GL_APIENTRY`, which only `gl2platform.h` (pulled in by `gl2.h`) defines; the reverse order fails to compile.
- [wayland] **`glTexImage2D`'s internal format must equal its data format on ES 2.0 (`GL_RGBA`, not `GL_RGBA8`).** Sized internal formats are ES 3 core; `GL_RGBA8` compiles as an enum but is the wrong token.
- [wayland] **A per-frame multi-tap fullscreen shader pass at native resolution can stall the shared poll loop.** A since-removed 96-tap glow blur froze the shell until moved to a downsampled FBO.
- [wayland] **A per-frame `glGenBuffers`/`glDeleteBuffers` for a static quad is driver churn the rest of the codebase avoids.** Create it once alongside the program/VBO it belongs to, like `Renderer::quad_vbo_`.
- [wayland] **A single-pass module shader can ride `Renderer::draw_custom` instead of owning FBOs.** `logout`'s `thunder_burst`/`thunder_shock` compile lazily, reuse `renderer/quad.vert` and `quad_vbo_`, and draw only their bounding box.
- [wayland] **A GPU-heavy overlay that still stalls the poll loop after tuning must move off it entirely.** The visualizer window runs on its own thread with its own share-context `EGLContext`.
- [wayland] **A render thread sharing an `EGLSurface` with the main thread must never be current on it simultaneously.** The render thread owns the surface while open; the main thread hands off a per-frame struct under a mutex.
- [wayland] **`toplevel_window_init_egl` leaves `EGLSurface` current on the main thread; a render-thread module must drop it via `app_detail::rest_egl_current`.** `visualizer` skipped this once; the render thread's `eglMakeCurrent` raced the main thread and lost with `EGL_BAD_ACCESS`, leaving the window unmapped.
- [wayland] **The shared context rests on `WaylandState::egl_rest_surface` via `app_detail::rest_egl_current`, never on a module's surface.** It is `EGL_NO_SURFACE` (surfaceless) or a 1x1 pbuffer fallback; resting on the first bar broke on unplug.
- [wayland] **A share context must use the main context's exact reset-notification attributes, or `eglCreateContext` fails with `EGL_BAD_MATCH`.** `visualizer` creates its share context from `WaylandState::egl_context_attribs`, never its own list.
- [wayland] **`gl_make_current` consumes the real EGL error before you can read it.** After it fails internally, the render thread's own `eglGetError` misleadingly logs `EGL_SUCCESS`.
- [wayland] **The default EGL swap interval blocks the poll thread on NVIDIA when the compositor withholds the buffer.** `gl_make_current` sets `eglSwapInterval(display, 0)` on every surface it binds.
- [wayland] **`wl_surface.frame` callbacks already pace every surface, so `eglSwapInterval(0)` doesn't cause tearing or a runaway loop.** Every `eglMakeCurrent`/`eglSwapBuffers` also checks its return and skips the frame on `EGL_FALSE`.
- [wayland] **Never drive GL for a surface the compositor has stopped compositing.** `wallpaper_paint` no-ops while covered or locked; a still-decoding wallpaper composites into the visible lock surface, not its own.
- [wayland] **Two hand-rolled opacity pipelines for one window's variants drift apart silently.** Special-casing let one path's opacity ignore fade-in; unifying onto one shared Renderer/Scene path fixed it.
- [wayland] **A single shared `Renderer` needs its own reset at frame start, not caller discipline.** Callers leaked stale opacity into the next paint; `Renderer::begin_frame()` now resets it itself.
- [wayland] **A freshly created share-context starts with `GL_BLEND` disabled, even sharing a namespace with an enabled context.** The visualizer render thread's raw GL path must call `glEnable(GL_BLEND)` itself once per context.
- [wayland] **`glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` squares alpha when a translucent rect draws over a transparent-cleared surface.** Use `glBlendFuncSeparate` with `GL_ONE` for alpha so it lands exactly where set.
- [wayland] **Renaming a persisted config value needs a load-time compatibility mapping, or users silently lose the setting.** Map old strings to new right after `value_or()` in `config.cpp`'s loader.
- [wayland] **An on-demand panel's own geometry should lock at open, not track live content.** Recomputing height every frame let content changes desync the close animation's start value.
- [wayland] **A `border_width` ported 1:1 from a QML `Rectangle` looks thinner here than in Qt.** The fixed 1px SDF antialiasing band eats more of its opacity; match `metrics::border_thin` (`2.0f`).
- [wayland] **`rrect.frag`'s distance must include the interior `min(max(q.x, q.y), 0.0)` term.** Without it `d` clamps at `-radius`, so a border wider than the radius filled the whole rect.
- [wayland] **Font hinting must differ between icon glyphs and text, not share one `cairo_font_options_t`.** `HINT_STYLE_NONE`+`HINT_METRICS_OFF` preserves Tabler stroke thickness while hinted text stays crisp.
- [wayland] **A `Texture`'s `width`/`height` are device pixels (`logical * scale`); laying out against logical coords breaks at HiDPI.** `launcher`'s row centring used raw `tex->height`; divide by `tex->scale` — the block was 2x oversized on scale 2.
- [wayland] **`Node`/`Scene` has no `z`; child claim order is paint order.** An overlay highlight must be claimed after every node it should sit above, not earlier.
- [wayland] **Skip the CPU BGRA→RGBA swizzle for shm capture buffers; `GL_EXT_texture_format_BGRA8888` uploads them directly.** A per-pixel swizzle per window per frame is a poll-loop stall; the GL path keeps recapture cheap.
- [wayland] **A settings tab's master toggle must early-`return` from its paint fn when off, not just render the switch.** `idle_tab_paint` kept drawing rows with idle disabled; click regions are paint-time, so gating paint gates input.
- [wayland] **A new settings tab must be added to `SettingsTab`, `kSettingsTabLabels`, `kSettingsTabs` and `kSettingsTabCount` at the same position.** `draw_nav_rail` maps rail row `i` to `SettingsTab(i)`, so a reordered list mis-routes tab clicks.
- [wayland] **An animated image's frame textures must be freed when its surface is off-screen, not held for the module's lifetime.** `AnimatedImage::hide` clears the textures and decode job; a later `show` re-uploads from the `.rgba` cache.
- [wayland] **One `dlopen`'d `libav` plugin (`media_plugin`) now decodes every animated surface, not only wallpaper.** The per-feature `ffmpeg` subprocess is gone; `animate_job_start` fills a `.rgba` frame cache.
- [wayland] **Hide the animated image after the close fade, not at close-start.** Clearing frames while `opacity` still tweens pops the image out; gate `hide` on fully closed.
- [wayland] **`animated_image_draw`'s `alpha` fades the whole image, border ring and ring-fill included, not just the frame texture.** It once skipped the ring, so `logout`'s logo frame lingered a beat after everything else faded.
- [wayland] **An `AnimatedImage` is bound to one source; use one instance per source, never re-point it.** `animated_image_set_source` leaves its `AnimateJob` stale and `animate_job_start` no-ops once `attempted`; `logout`'s static toggle kept showing gif frames.
- [wayland] **The `.rgba` cache key must include every decode-shaping param, such as `fit`.** Otherwise frames cached under the old shape (square-cropped) are silently reused after the decode changes.
- [wayland] **Every ad-hoc media-to-cache path is the same four steps: XDG-cache-dir, mtime hash key, decode, existence check.** `media_service` owns them; no per-feature copy.
- [wayland] **A bounded UI-gif decode still needs `fps * seconds` frames, not a fixed count.** A `36`-frame cap once showed only the first `2.4s` of a `7s` logo; keep a generous ceiling.
- [wayland] **Decode frames into a `.tmp` dir and rename on success, never straight into the cache dir.** A killed decode left a partial frame set that every later run reused as complete.
- [wayland] **`fs::rename` of the `.tmp` frame dir must `remove_all` the target first; `rename(2)` into a non-empty dir fails.** A stale `.png`-format cache dir made every gif promotion fail silently.
- [wayland] **A "cache already built" check counting one file extension doesn't prove the dir is absent.** `count_rgba_frames` saw zero and left the old `.png` dir in place, blocking the rename.
- [wayland] **An `AnimatedImage` decode resolution must be device px (`logical * scale`), not logical px.** Decoding the lock avatar at its `200` logical size upscaled it blurrily on scale 2.
- [wayland] **An animated wallpaper needs no pre-copy into the cache.** `media_plugin` reads the source path directly and libav probes by content; the copy only added a thread.
- [wayland] **A closed full-screen overlay/panel must no-op its own `request_frame`, gated on `open`.** Blanket frame fan-outs otherwise repaint every idle surface, spiking the GPU on any toggle.
- [wayland] **`notification_paint` re-arms only while a tween is active, so every time-varying element must be tween-driven.** The wall-clock timeout bar froze between the entrance tween and expiry; a per-entry linear tween fixes it.
- [wayland] **`animated_image_animating` must exclude single-frame stills (`frame_count > 1 && fps > 0`).** A static logo otherwise reports as animating, so overlays repaint every frame for the panel's open life.
- [wayland] **A live-preview overlay paces its re-arm to its capture interval, not the display refresh.** `overview` re-armed every `frame_done`; a `tick` at `kOverviewCaptureIntervalMs` cuts idle GPU with no visible change.
- [wayland] **Variable-count node colours come from a frame-scoped `static thread_local std::deque<Color>` cleared at build start.** `deque` keeps element pointers valid across `push_back`; `static` outlives the build call so pointers survive to `draw`.
- [wayland] **Reusing `arc_gauge` at a larger diameter must scale the stroke by the reference's stroke/diameter ratio.** `lock`'s ~3x gauges kept the 68px reference gauge's 6px stroke and rendered as a hairline; `kLockResGaugeStrokeRatio` (`6/68`) fixes it.
- [wayland] **Exact-texel reads in ES 2.0 use `texture2D` with `GL_NEAREST`; add the `+0.5` UV offset only to explicit integer indices.** `gl_FragCoord.x` already sits at `i + 0.5`; adding another landed on the boundary with texel `i+1`, where `GL_NEAREST` is undefined.
- [wayland] **`visualizer/fullscreen.vert` emits only `gl_Position`; a fragment shader paired with it must use `gl_FragCoord`, not a `vUv` varying.** A `vUv` input has no matching vertex output, so the program fails to link and the visualizer clears.
- [wayland] **The sphere's point-sprite accumulator must be a `GL_FLOAT` render target, not `GL_UNSIGNED_BYTE`.** Hundreds of overlapping splats per pixel clamp at `1.0` in `UNORM`, starving `sphere2_main.glsl`'s brightness curve and the glow pass.
- [wayland] **Float accumulation with additive blending on ES 2.0 needs `GL_OES_texture_float`, `GL_EXT_color_buffer_float`, and `GL_EXT_float_blend`.** Probe the extension string before relying on them elsewhere; this hardware has all three.
- [wayland] **`ncs`'s `sphere.radius` is an exclusion-disc radius, not the blob's size.** Particles inside it evacuate onto a shell; a too-small radius shows a rectangle around a punched hole.
- [wayland] **Every `ncs.glsl` default constant must stay verbatim; the shader is a tuned whole, and "roughly similar" tweaks read wrong.** Reverted divergences: `sphere.radius = 0.5 * min(...)` (rectangle-with-hole) and a `baseForm.scale` override that should stay `2.0`.
- [wayland] **The canvas must stay square, or `sphereCoords()` yields an ellipsoid instead of a sphere.** Blob geometry (`sphere.radius`, `bassMultiplier`, `displacements`) is pixel-absolute against the canvas via the `resolution` uniform.
- [wayland] **`visualizer`'s render thread must present every frame and start its fade on the first presented frame, not budget-gated.** An earlier build skipped `eglSwapBuffers` over `60ms` and delayed the fade until under `30ms`, pinning it at `0`.
- [wayland] **`ncs`'s `time` uniform is a frame counter paced to `fps` (default `60`); its decay constants are per-frame at that rate.** An un-paced port on a high-refresh display scrolls noise and decays the spectrum too fast, reading as over-reactive.
- [wayland] **`load_image_decode` renders SVGs into a square viewport only.** Aspect-correct sprites need a direct `librsvg` render into a sized `cairo` surface.
- [wayland] **A cell-snapped rain column varies its fall rate by skipping ticks via a fractional accumulator, not a fractional step.** A fractional per-tick advance desnaps `matrix_rain`'s glyphs from the cell grid; `stiletto_rain`'s free sprite scales the step directly.
- [wayland] **Driving `visualizer` from the poll-thread `FrameClock` instead deadlocked the frame pump on Mesa.** `visualizer_toggle` clears `base.frame_clock.surface` so nothing arms a `wl_surface_frame` behind the render thread's back.
- [wayland] **A global "instant" switch inside `AnimationManager` can't safely reach a perpetually self-re-arming `on_complete` chain.** Forcing every step to `0 ms` recurses synchronously inside `tick()` forever; `marquee_scroll` checks the switch itself and skips starting instead.
- [wayland] **`MarqueeTextState::marqueeing` means scrolling, not overflowing.** Clip on `texture width > box width`, or long text overflows while scrolling is suppressed.
- [wayland] **A concave hug corner is the same quarter-circle cutout as a convex fillet.** `frame.cpp` reuses `fillet_rgba`, changing only size and position.
- [wayland] **`icons.h`'s Tabler glyph codepoints can't be safely extended without font-inspection tooling this environment lacks.** No `fontTools`/`otfinfo`/`ttx` installed; a guessed codepoint risks silently rendering the wrong glyph.
- [wayland] **libpng/libjpeg report errors by `longjmp`, skipping every `delete[]` and C++ destructor after the `setjmp`.** PNG uses `png_image`; JPEG holds its buffer in a `volatile` pointer freed in the `setjmp` branch.
- [wayland] **libjpeg's default `error_exit` calls `exit()`, so one corrupt JPEG kills the shell.** `decode_jpeg` installs `jpeg_error_exit`, which `longjmp`s back and returns `nullptr`.
- [wayland] **Never size an allocation from an on-disk header without checking the file size.** A corrupt `.rgba` cache header made `new[]` throw on a worker thread, calling `std::terminate`.

### Wayland: 3. Wayland protocol

- [wayland] **Wayland gives no way to query live which output the pointer is over.** Track it as a best-effort hint from your own surfaces' enter/motion events instead.
- [wayland] **Optional protocol events need sane fallback defaults, not zero.** Some compositors never send `repeat_info`; defaulting to 0/0 silently disables key repeat.
- [wayland] **Never live-test input-grabbing or lock-screen protocol code carelessly.** A prior live test hung the keyboard and forced a reboot.
- [wayland] **A coordinate from one layer-shell surface isn't valid on another without translating margins.** Different surfaces can have different margins, so origins don't automatically align.
- [wayland] **A fading, resizing surface should tween opacity only and snap geometry at the endpoints.** Driving both from one tween made content visibly rescale instead of cleanly fading.
- [wayland] **Hover-driven surface changes must only touch size, never margin or exclusive_zone.** Changing margin on hover repositioned the surface mid-hover, causing an infinite flicker loop.
- [wayland] **`exclusive_zone` must not include the same-edge margin value.** The compositor already adds that margin automatically, so including it reserves the space twice.
- [wayland] **An output's initial event burst isn't guaranteed by the same roundtrip that bound it.** A bind's reply is a second round-trip; do one extra roundtrip before reading output state.
- [wayland] **A live-update IPC event may lack a field only a full snapshot query provides.** Check whether an adjacent event in the same stream already carries and can cache that field.
- [wayland] **Don't start the polkit agent before keyboard input and a GLib main loop both exist.** An agent that registers but can't prompt intercepts and fails every real `pkexec` system-wide.
- [wayland] **A struct member can't share a name with a Wayland protocol type used in the same header.** `xdg_surface *xdg_surface` compiles but breaks name lookup in including translation units; rename the field.
- [wayland] **A real `xdg_toplevel` window is created on open and destroyed on close, not kept mapped-but-transparent.** A zero-opacity mapped toplevel would still show in switchers, unlike a layer-shell fade-in-place.
- [wayland] **`ToplevelWindowBase` has no generic resize callback, only `on_close_request`.** A module keeping a persistent per-size buffer must compare against live width/height each paint.
- [wayland] **An animation's `on_complete` that destroys the animated surface can fire mid-frame, inside paint's own `tick()`.** Re-check surface validity right after `tick()`, or defer the destroy to the next poll iteration.
- [wayland] **A bottom-anchored layer surface changes its size and buffer in one commit.** Sway repositions from the requested size at once; a separate commit dropped hover.
- [wayland] **A layer-shell overlay stays mapped across same-output toggles.** Destroy-then-recreate left Hyprland's layer stack stale until an unrelated event.
- [wayland] **Destroying and recreating a layer surface with the same namespace can leave it uncomposited.** Hyprland kept the reopened panel invisible until an unrelated workspace switch or exclusive-zone change.
- [wayland] **A module with its own hand-rolled toggle can silently miss a shared surface-lifecycle fix.** `launcher_toggle` kept destroying its surface every close, hitting the same uncomposited-Hyprland-surface bug independently.
- [wayland] **A recreated same-namespace layer surface can paint yet miss pointer routing.** Keep layer surfaces mapped across toggles.
- [wayland] **Tearing down a `ToplevelWindowBase` must also reset its `FrameClock` fields, not just Wayland/EGL handles.** A stale non-null `frame_clock.callback` makes `request_frame` silently no-op forever afterward.
- [wayland] **A pending `wl_callback` must be released with `wl_callback_destroy`, never just nulled.** `wl_surface_destroy` doesn't free it; a late `done` then fires against a reused, reset struct.
- [wayland] **Release an `EGLSurface` from the current context before `wl_egl_window_destroy`, not after.** `eglDestroySurface` on a current surface defers teardown; the freed `wl_egl_window` is then read, corrupting EGL state.
- [wayland] **Every `EGLSurface` teardown calls `gl_release_if_current` before `eglDestroySurface`.** It unbinds only when that surface is current, keeping the context bound surfacelessly instead of dropping it.
- [wayland] **Matrix and visualizer are meant to tile as regular windows, not float.** Do not add a `float = true` window rule; that's a rejected direction, not an oversight.
- [wayland] **A destroy-on-close `xdg_toplevel` can hand its next window handle the exact address a prior instance had.** Per-window state keyed by address must clear when the window goes away, or it inherits stale state.
- [wayland] **In `hl`'s Lua event API, a window's fields are only safe to read on `window.close`, not `window.destroy`.** By `window.destroy` fields read back `nil`; assigning to a nil table key crashes Lua.
- [wayland] **A best-effort pointer-hint into a per-output struct must be cleared on that output's removal.** `wl_output` removal frees its `MonitorOutput`; a stale `last_pointer_monitor` caused a use-after-free crash.
- [wayland] **A mostly-click-through layer surface can still take clicks on sub-rects via a `wl_region` union of them.** The notification surface rebuilds its region from visible close-button rects each paint; the rest stays click-through.
- [wayland] **A `PerMonitorModule::handle_pointer_move` fires on every monitor's instance with the same shared coords.** Guard hover state on `pointer.focused_surface == own surface`, or every monitor highlights at once.
- [wayland] **The pointing-hand cursor needs `wants_pointing_hand_cursor()` on both `Module` and `PerMonitorModule`.** The poll loop only asked overlays, so `bar` and every unimplemented module kept the arrow.
- [wayland] **`ext-session-lock` withholds the `locked` event until every output presented a non-null lock-surface buffer.** Create and paint all lock surfaces before flushing; a null-buffer commit is a protocol error.
- [wayland] **`unlock_and_destroy` needs a `wl_display_roundtrip` before the lock surfaces are torn down.** The protocol warns the server may kill the client with a protocol error otherwise.
- [wayland] **A lock-surface `configure` must be `ack`ed before any commit and re-acked on every resend.** Defer the `wl_egl_window` resize past the handler; resizing inside it causes reconfigure errors.
- [wayland] **An unlock animation's `on_complete` must defer surface teardown via `DeferredCall`, never free inline.** It fires inside `AnimationManager::tick()`, mid-iteration over the very manager the teardown destroys.
- [wayland] **The startup overlay `init_egl` loop must not gate on `surface()`.** The lock owns no surface until locked; gating skipped its `init_egl`, leaving draw state unset.
- [wayland] **A `pam_start_confdir` service needs an `account` rule, not just `auth`.** With none, `pam_acct_mgmt` returns `PAM_PERM_DENIED` and a correct password still fails.
- [wayland] **A `ToplevelWindowBase` with a dedicated render thread must not also be wired into the poll-thread `FrameClock`.** The `FrameClock` arms `wl_surface_frame` on the poll thread, but the render thread's `eglSwapBuffers` is what actually commits.
- [wayland] **On Mesa, that split made the `wl_egl` frame pump die after one frame; NVIDIA's private `wl_event_queue` was unaffected.** `visualizer` never advanced its fade, showing only Hyprland's border, while the mismatched traffic also hitched the poll loop.
- [wayland] **Every per-monitor `destroy()` must drop its pending frame `wl_callback`; `wl_surface_destroy` does not.** On hotplug, module state can free mid-callback, so a late `frame_done` runs on freed memory.
- [wayland] **A `DeferredCall::call_later` closure that captures per-monitor module state by reference outlives that state on output hotplug.** Wallpaper decode closures now capture a `std::weak_ptr<int>` lifetime token by value and bail before the generation check.
- [wayland] **`registry_global_remove` must tell `app.overlays` an output is gone via `Module::on_output_removed`.** An overlay bound to the unplugged output otherwise keeps a mapped surface and a dangling `bound_output` pointer.
- [wayland] **A per-monitor `TextInputClient`'s own `destroy()` must clear its own text-input focus, not rely on `Module::on_output_removed`.** `PerMonitorModule::destroy()` bypasses `on_output_removed`; `BarPerMonitorModule::destroy()` skipped `sync_text_input_focus(false)`, leaving a dangling `active_client_` that crashed.
- [wayland] **A per-monitor layer surface's `.closed` handler must be a no-op, like every sibling module's.** `registry_global_remove` already owns per-monitor cleanup; a handler setting `app->running = false` silently exits the whole shell.
- [wayland] **An overlay opened from a bar widget needs its own `*_here` term in `bar_paint`'s `want_shown`.** Opening the overlay steals pointer focus from the bar, so `autohide` collapses it; `logout_here` and `overview_here` prevent that.

### Wayland: 4. Async state correctness

- [wayland] **Never score an async operation's result against a live mutable field.** Freeze the input into its own field at start time and score against that instead.
- [wayland] **Capture a value synchronously at the action site rather than deferring to the next repaint.** Relying on an async dispatch path left panels opening at position zero on first use.
- [wayland] **Click coordinates must be captured atomically with the click event itself.** Reading the live shared pointer position later can return a different monitor's coordinates.
- [wayland] **A value read right after triggering a state change can still be one frame stale.** Force the downstream paint/tick to run before reading its side effect.
- [wayland] **Two `animate()` calls sharing an owner id cancel each other, even if unrelated.** Give each simultaneously-animated property of an item its own distinct owner id.
- [wayland] **AnimationManager only advances when something calls `tick()` every frame.** A panel that forgets to tick freezes forever, including keyboard-release `on_complete` callbacks.
- [wayland] **A reactive `*_changed` flag must be set at every code path that changes the value.** One mutator forgetting the flag silently breaks reactivity for just that path.
- [wayland] **Binding a PipeWire node listener alone doesn't deliver live param-value updates.** An explicit `pw_node_subscribe_params()` call is required to receive future value changes.
- [wayland] **Every panel requesting exclusive keyboard interactivity needs its own key-dispatch arm.** The two are declared separately, so nothing enforces they stay in sync as panels are added.
- [wayland] **An optimistic local write can suppress the `*_changed` flag it's supposed to trigger.** The confirmation compares against the already-updated value and finds no change; raise the flag at the write.
- [wayland] **A modifier-aware shortcut must match `KeyEvent::base_sym`, not `text`.** `text` is modifier-transformed (`Shift`+`1` is `!`, `Ctrl`+`d` is `\x04`), so `overview`'s digit and `d` shortcuts never fired.
- [wayland] **A client's own callback confirming a write isn't proof the real state changed.** Device-backed PipeWire nodes need writes routed through the parent Device's Route, not the node.
- [wayland] **A registry's initial announcement and an object's own info event carry different properties.** A property missing from one may only appear in the other's later event.
- [wayland] **A generic "click missed" guard excluding a sibling surface pushes the decision onto it.** That surface's own handler must then know about every overlay stacked above it.
- [wayland] **Calling a shared AnimationManager's `tick()` multiple times per instant is safe if absolute-time-based.** It recomputes from wall-clock time, not accumulated delta, so repeats don't double-advance.
- [wayland] **A hover-driven highlight must clear on lost surface focus, not just recompute on motion.** Another surface stealing pointer focus mid-hover leaves a stale hovered index otherwise.
- [wayland] **A highlight teardown that calls a recompute helper must null the source indices first.** `logout`'s close re-read live `selected_index`/`hovered_index`, so `update_highlight` re-lit the button through the exit.
- [wayland] **A mutex must cover the read side of a shared buffer, not just the write side.** Ring-buffer reads outside `mutex_` raced the PipeWire thread's writes.
- [wayland] **A local sdbus proxy destroyed right after firing an async call corrupts the shared connection it borrowed.** Cache one proxy per object path on the owning state instead of a throwaway per call.
- [wayland] **Adding a second writer to shared per-bin state should prompt auditing every existing writer.** `on_param_changed` recomputed FFT bin ranges off-mutex, harmless until bar-count changes made it frequent.
- [wayland] **A sync-from-config function uploading only on non-empty paths must also clear the texture when the path empties.** The fix resets the column's `Texture` and bumps its generation counter to reject stale decodes.
- [wayland] **Clearing a texture in memory doesn't repaint the surface, only uploading one does.** Both branches must call `wallpaper_request_frame`; the empty-path clear skipped it, so the old wallpaper stayed.
- [wayland] **A widget/IPC-opened panel must prime its polled telemetry at the open site, not the timer tick.** The control center's cards popped in one-by-one over seconds; the CPU pill already primes on click.
- [wayland] **An async result polled on a slow throttle lands a throttle-period late, not a request late.** `gpu_temp_poll` starts `nvidia-smi` on one call, reads it next; poll every tick while running.
- [wayland] **Re-enabling idle management, or lowering a timeout mid-idle, must reset the per-monitor activity clock.** A stale `last_activity` otherwise fires the screensaver instantly; `apply_config_update` calls `idle_reset` on any idle-config change.
- [wayland] **A panel's staged dismissal must be coded identically in every dismiss path.** `Escape` collapsed the subpanel while outside-click closed the whole panel, because the branches were written separately.
- [wayland] **A closing overlay's `request_frame` call must run before its fade's `on_complete` flips `open` false, not after.** `overlay_panel_request_frame` no-ops once closed; a zero-duration animation runs `on_complete` synchronously, so a trailing call never arms the final frame.
- [wayland] **`CompositorState` is not ready when the first monitor's surface is created.** `main()` builds surfaces before services; sizing from compositor state needs a later catch-up.

### Wayland: 5. Architecture and scale discipline

- [wayland] **Check a reference technique against the full target hardware range, not one profiling machine.** An integrated-GPU-only measurement wrongly justified diverging from a technique discrete GPUs need.
- [wayland] **Hardware decoder selection stays portable by trying a preference list of `AVHWDeviceType`s, not branching on hardware.** Tries `CUDA` then `VAAPI`, falling through to software.
- [wayland] **A GPU zero-copy texture import mechanism is vendor-specific, unlike hardware decode selection.** `VAAPI`'s `DMA-BUF`/`EGLImage` trick needs Mesa; `CUDA`-GL interop is a separate NVIDIA mechanism.
- [wayland] **`AVCodecContext::get_format` can't capture lambda state, only a plain function pointer.** `media_plugin.cpp` passes the wanted hardware pixel format through `codec_ctx->opaque` instead.
- [wayland] **A decode filter graph can't be built before the first frame decodes.** `CUDA`/`VAAPI` transfer format varies by driver; the graph builds from the first decoded frame.
- [wayland] **Looping in-process decoded video needs a seek-and-flush, not a process restart.** `av_seek_frame` plus `avcodec_flush_buffers` on EOF replaces the `ffmpeg` CLI's loop flag.
- [wayland] **A decoder can hold a frame back internally, released only by the next `send_packet` or a flush.** Flushing before draining drops it; send a nullptr flush packet and drain first.
- [wayland] **A glyph missing from the primary font shifts the whole line's baseline.** Pango's fallback inflates ascent; use the em dash instead of `U+00B7`.
- [wayland] **`~` in a path is a display convention, never a real path.** `std::filesystem` never expands it; `core/path_home.h` collapses `$HOME` at UI/JSON edges, and every path passes `path_expand_home` before use.
- [wayland] **`Config` path defaults must already be absolute, not `~`-prefixed.** `load_config()` returns the default `Config` unexpanded when no file exists, and the wallpaper picker scans it directly.
- [wayland] **A default-plus-override config value must be cached on the consumer's own per-monitor state.** Re-resolving the tier chain on every hot-path read would turn 15 reads into map lookups.
- [wayland] **A resolved-with-fallback accessor and a raw-override accessor answer different questions.** A "remove override" control needs the raw override only; the fallback resolver makes it no-op wrongly.
- [wayland] **Porting a singleton overlay to per-monitor rendering must split process-wide from per-monitor state.** A D-Bus connection is one per process; the render surface is one per monitor.
- [wayland] **Extending a fallback-inclusive resolver to a second mode needs its own raw-override accessor too.** Adding an animated-column fallback required a matching `_override` accessor to avoid the same pitfall.
- [wayland] **A fallback gated on `column_index == 0` isn't global, it's whichever monitor resolves column 0 first.** A truly global toggle needs the same check on every column, everywhere.
- [wayland] **A single-instance overlay bound to one output must not have its open-state read per-monitor.** Every monitor's `bar_paint` saw `logout` open and expanded everywhere; gate on `bound_output() == mon.output.wl`.
- [wayland] **A per-monitor `apply_config` must read its `new_cfg` argument, never `app.cfg`.** `apply_config_update` assigns `app.cfg` only after the fan-out, so `app.cfg` is still the old config.
- [wayland] **A per-monitor module's `create_surface()` must not gate on config state.** It runs once with no re-entry; wallpaper gated surface creation on a startup check, breaking toggle-on.
- [wayland] **A "prepare" step that pre-scales/fps-caps a video duplicates work the live decode filter graph already does.** The software `libx264` transcode caused the CPU spike it aimed to avoid; removed.
- [wayland] **A zero-copy DRM delivery path bypasses the filter graph entirely, so an `fps=` filter there fixes nothing.** `media_plugin.cpp`'s VAAPI zero-copy branch delivers straight from the decoder, skipping `ensure_filter_graph`.
- [wayland] **A fixed per-decoded-frame sleep throttles decode rate, not display rate, ignoring source timestamps.** Pace playback via each frame's pts against a wall-clock anchor; drop early frames, re-anchor on seek.
- [wayland] **One detached decode thread per picker tile bloats RSS for good.** About 35 concurrent decodes made glibc keep eight 64 MB arenas.
- [wayland] **A thumbnail decode must scale inside the filter graph, not decode native then downsample.** `decode_first_frame` takes a target box and emits `scale=W:H,format=rgba`, so `MediaFrame.rgba` is KB not MB.
- [wayland] **First-frame/frame-set decodes pin `avcodec` `thread_count = 1`; frame-threading buys nothing and explodes arenas.** Only `decode_loop` (streaming, VAAPI) stays frame-threaded.
- [wayland] **`main()` sets `mallopt(M_ARENA_MAX, 2)`.** astralia-shell's steady-state allocation is main-thread-dominated; capping arenas stops any decode burst permanently inflating RSS.
- [wayland] **`main()` also pins `M_MMAP_THRESHOLD` and `M_TRIM_THRESHOLD` to 256 KB.** Glibc's dynamic thresholds otherwise rise after each large decode, leaving a worker arena holding about 47 MB free.
- [wayland] **A JPEG decode given a target size uses libjpeg `scale_denom`, not a native decode.** `load_image_decode`'s `hint_w`/`hint_h` cut the first-frame decode peak from about 610 MB to 481 MB.
- [wayland] **Brightness is set through `brightnessctl`, never a direct `sysfs` write.** `/sys/class/backlight/*/brightness` is root-only without a `uaccess` udev rule; `brightnessctl` routes through `logind`.
- [wayland] **A dock icon must resolve through a `.desktop` id/`StartupWMClass` -> `Icon` heuristic, not the raw compositor window class.** Passing the raw class straight to the icon-theme lookup collapses every unresolved app onto one generic placeholder icon.
- [wayland] **The main poll loop must `continue` on `EINTR` and `klog` any other `poll()` error before breaking.** Breaking on `EINTR` silently exited the whole process, masquerading as an unexplained crash on display unplug.
- [wayland] **`main()` must ignore `SIGPIPE` and install `klog_install_crash_handler()` to get any post-mortem from a crash.** `SIGPIPE`'s default action terminates without a core, and the crash handler logs a backtrace for `SIGSEGV`/`SIGABRT`/`SIGBUS`/`SIGILL`/`SIGFPE` into `astralia.log`.
- [wayland] **`klog_install_crash_handler()` also installs a `std::set_terminate` handler that `klog()`s the uncaught exception's `what()`.** `daemonize()` sends `stderr` to `/dev/null`, so `libstdc++`'s terminate diagnostic otherwise never reaches the log.
- [wayland] **A `backtrace_symbols_fd` crash trace maps to source only with `addr2line` against the exact binary that crashed.** It resolves only dynamic symbols, so internal frames print as raw `astralia(+0x...)` offsets that a rebuild invalidates.
- [wayland] **A file writer must create its own target directory, not assume something else did.** `write_file_atomic` silently failed `save_config()` on fresh installs; other writers already `mkdir()` first.
- [wayland] **Merging a module's pure logic and EGL/GL code into one file forces graphics deps onto the test binary.** Keep the `*_test_sources`/`*_main_only_sources` split so the test binary stays free of EGL/GL.
- [wayland] **astralia-shell has no runtime shader preprocessor; flatten ported multi-file shaders at authoring time.** `visualizer` inlines every `#include` and hand-expands `#expand` into `assets/shaders/visualizer/sphere/*.glsl` fragment files, concatenated at runtime by `visualizer_shaders.cpp`.
- [wayland] **Every bundled asset needs the installed-path-plus-dev-tree-fallback loading pattern.** A bare relative path resolves against the daemon's cwd, silently failing outside the source tree.
- [wayland] **A connect()-to-socket liveness probe is unreliable against a leftover socket file.** Prefer a flock()-guarded lock file, which the kernel releases automatically on process death.
- [wayland] **A generically-named `constexpr` constant can collide with an identical name in an unrelated header.** Two modules that never include each other can still land in the same translation unit transitively.
- [wayland] **A module can't include another module's header, and `main.cpp` can't name a module's function directly.** Cross-module orchestration — IPC verb table, key-dispatch table — lives in `src/app/` instead.
- [wayland] **A module's `Module`/`PerMonitorModule` adapter lives in its own `.cpp`, exposed as a `make_*_module` factory.** Cross-module needs, like `lock`/`idle` drawing wallpaper, arrive as hooks injected by `module_registry.cpp`.
- [wayland] **Removing a UI feature's draw code but leaving its click-kinds, state field, and handlers reads as live.** The settings dropdown kept `open_dropdown_id`, two `PanelClickKind`s, and handler cases after its last caller went.
- [wayland] **Bar geometry reads `BarStyleSpec` (`top_margin`, `side_margin`), never `kBarTopMargin` or `kPanelSideMargin` directly.** Each style attaches differently; a leftover constant misplaces panels and hit-testing.
- [wayland] **A bar style needs a row in `kBarStyleNames`, `kBarStyleLabels` and `kBarStyleSpecs`, plus a `kBarStyleCount` bump.** A too-short table silently zero-fills, leaving a null label pointer.
- [wayland] **A project rename can't reuse one identifier style everywhere.** `astralia-shell` is kebab-case for binary, paths and PAM; `ASTRALIA_SHELL_` macros; `astralia_shell_` symbols; D-Bus paths use underscores, as hyphens are forbidden.

### Wayland: 6. Hyprland IPC

- [wayland] **This user's Hyprland build has the classic `dispatch <dispatcher> <args>` string protocol deprecated for Lua.** `hyprctl dispatch <X>` is shorthand for `hl.dispatch(X)`; `X` must be a `hl.dsp.*` call.
- [wayland] **`hypr_dispatch`'s transport (`"dispatch " + command` over the request socket) is correct; only the argument shape was wrong.** It had zero callers until `hypr_tile_*` proved a `hl.dsp.*` Lua-expression string works.
- [wayland] **`hypr_refresh` must also run on `activewindowv2`, not just structural events.** Focus changes bump every client's `focusHistoryID`; UI ordered by it goes stale without a re-read.
- [wayland] **Hyprland emits no event for a tiled reorder inside a workspace.** `movewindowv2` is workspace-move only; re-read `j/clients` each second and redraw on change.
- [wayland] **Every Hyprland `request()` on the poll thread needs a socket timeout, read as "no change".** An empty reply parsed as an empty client list would blank the dock.
- [wayland] **An overlay reading compositor state on open should `compositor_refresh` first.** Event-driven state can be arbitrarily stale by the time the user opens the panel.
- [wayland] **`redraw_all_monitors` pokes only per-monitor modules, never `app.overlays`.** An open app overlay reacting live to an event needs its own `request_frame()` loop over `app.overlays`.
- [wayland] **The bar's per-monitor workspace pills carry the compositor's absolute workspace id.** Switching from a pill must call `compositor_focus_workspace` with `global=true`, or Hyprland's `resolve_workspace` remaps it.
- [wayland] **A bar widget that only emits scene nodes isn't clickable until its hit rects are recorded and routed.** `dispatch_pill_click` scans only the fixed `PillId` array; the workspace row stores and checks its own rects.
- [wayland] **A workspace grid spanning monitors must pass `global=true` to every `hypr_tile_*` call.** `resolve_workspace` otherwise remaps ids `1..10` onto the focused monitor's page.
- [wayland] **`hyprctl getoption <name> -j`'s `j/getoption <name>` command works unmodified over the existing request socket.** No need to shell out; live-verified `general:gaps_out`/`decoration:rounding` return the same JSON either way.

### Wayland: 7. Telemetry and structure

- [wayland] **Intel iGPUs often expose no GPU `hwmon`, yet still have `/sys/class/drm/card*/gt_act_freq_mhz`.** `find_gpu_clock_sensor` falls back to scanning `/sys/class/drm` so the clock still shows.


## X11 backend (from `i3`)

- [x11] Wall-clock timers use a `CLOCK_BOOTTIME` `timerfd`. Monotonic clocks pause in suspend, leaving the clock stale after resume.
- [x11] Never wipe the EWMH connection after `xcb_ewmh_init_atoms_replies` fails. Its failure path already frees everything, so wiping double-frees.
- [x11] Strings with embedded NULs, such as `WM_CLASS`, need the `sv` literal. A `std::string_view` from `const char*` stops at the first NUL.
- [x11] Take the single-instance `flock` before `daemonize()`. The child inherits the lock, so it survives the parent's exit.
- [x11] The `kill` IPC client releases its fd instead of closing it. The kernel closes it at shell exit, so the client returns afterwards.
- [x11] Under i3, dock windows ignore requested position and struts. Inset content inside a full-width ARGB dock instead.
- [x11] A 32-bit window needs its own colormap and a border pixel. Otherwise window creation fails with `BadMatch` against the root depth.
- [x11] Icons use the explicit `tabler-icons` Pango family. Tabler codepoints collide with Codicons inside the Nerd Font.
- [x11] Call `register_app_fonts()` before the first `Text` exists. Pango snapshots fonts on creation and misses fonts added later.
- [x11] Round-trip before `xcb_disconnect`. It does not sync, so Xorg drops unprocessed requests and loses exit cleanup.
- [x11] Never set `ESETROOT_PMAP_ID` on the shell's own pixmap. Wallpaper setters kill its owner, which would disconnect the whole shell.
- [x11] The wallpaper destructor must reset the root background and delete `_XROOTPMAP_ID`. Otherwise the root keeps the image alive.
- [x11] Never `cairo_device_finish` a cairo-xcb device on the shell connection. The device is shared, so finishing it breaks the bar.
- [x11] RandR notify events carry no event window. Route them with `EventLoop::on_event`, since the root handler belongs to the workspace service.
- [x11] Watch a config file's directory with `inotify`, filtered by name. Editors save by rename, which drops a watch on the file.
- [x11] `SystemBus` polls both the bus fd and its `eventFd`. Sync calls queue signals internally, and only `eventFd` wakes the loop.
- [x11] Spawned children need an empty signal mask and default `SIGPIPE`. The shell blocks and ignores signals, and exec inherits both.
- [x11] Overlays take input focus, never an active keyboard grab. A grab blocks the window manager's hotkeys, so toggles never arrive.
- [x11] Read xkb modifiers from each key press's `state` field. Overlays opened by a Shift hotkey miss its release.
- [x11] Disable cairo MIT-SHM on a probe surface right after connecting. Surfaces copy the flag at creation, and its pool stays resident.
- [x11] Disable cairo SHM with version `-1, -1`. Cairo checks for negative versions, so `0, 0` leaves SHM on.
- [x11] Decode images with `stb_image` and `resvg`, not gdk-pixbuf. gdk-pixbuf pulls in sandboxed loaders, extra threads and megabytes of libraries.
- [x11] Call `malloc_trim(0)` after bursts like decodes or broad searches. glibc keeps freed small chunks, pinning several megabytes otherwise.
- [x11] Slow idle heap growth is glyph-cache warm-up plus fragmentation, not a leak. `heaptrack` showed live heap flat after three idle minutes.
- [x11] Drive GLib through an `EventLoop` poll source. Polkit and GDBus use changing fds that fixed fd watches miss.
- [x11] Wipe password buffers with `explicit_bzero` after responding or cancelling. `std::string::clear` leaves the bytes in the heap.
- [x11] Hold one sdbus proxy per fixed object, but keep changing NetworkManager paths one-off. Caching per-reconnect paths grows without bound.
- [x11] `AudioService::changed` also fires `AudioKind::nodes` after every volume change. Match each kind explicitly, never with a sink-or-else fallback.
- [x11] Call `EventLoop::reschedule()` when an event moves a timer's deadline earlier. Deadlines are only recomputed after firing.
- [x11] Never destroy an sdbus proxy inside its own async reply callback. Prune per-device proxies from the signal-match handler instead.
- [x11] A detached reader thread must own what it touches through a `shared_ptr`. The owner may be destroyed before the child exits.
- [x11] Map a popup beside a panel with `show(false)`, never focus. Taking focus fires the panel's focus-out close; its `owner_events` grab still routes clicks.
- [x11] SNI items signal `NewIcon`/`NewStatus`, rarely `PropertiesChanged`. Refetch `GetAll` on those signals, or icons go stale.
- [x11] Hide bars for unplugged or disabled outputs, never destroy them. `EventLoop` cannot remove windows or timers, so they would dangle.
- [x11] Let one `OutputService` own the RandR notify. `EventLoop::on_event` keeps a single handler per type, so a second owner replaces it.
- [x11] Decode wallpaper thumbnails off the main thread, through the cover cache. A full `4K` decode takes about half a second.
- [x11] Set `M_ARENA_MAX` to 1 and a fixed `M_MMAP_THRESHOLD`. A decode thread's private arena kept about 50 MB after the tab closed.
- [x11] Wrap decoded `stb_image` pixels in the cairo surface in place. Copying doubled the transient peak of large wallpapers.
- [x11] Decode JPEGs with `libjpeg` scale denominators of 2, 4 or 8. Reduced-size decoding skips most pixels and memory.
- [x11] Touch a cache file's mtime on every hit. Pruning treats mtime as last use, so wallpapers in use survive.
- [x11] Cache opaque covers as JPEG and transparent ones as PNG. JPEG writes faster and is far smaller on the X201's slow disk.
- [x11] Keep thumbnails as X-side surfaces made with `cairo_surface_create_similar` on first paint. Image surfaces upload to the X server on every paint.
- [x11] Batch repaints from worker results through a timer, and reschedule only when no repaint is pending. Rescheduling on each arrival delays the paint while results keep coming.
- [x11] Run the thumbnail worker at nice `10`. On the X201's two cores, decoding otherwise competes with the UI thread.
- [x11] A nested `Xephyr` shows no cursor and ignores XTest clicks for the shell. Click inside its window by hand.
- [x11] Paint overlapping translucent shapes with outer borders, then `CAIRO_OPERATOR_SOURCE` inner fills. `OVER` double-darkens where shapes overlap.
- [x11] Derive tiled rects from `layout` and `percent`, not `GET_TREE` rects. i3 skips rendering hidden workspaces, leaving them stale.
- [x11] Ignore focus loss briefly after an i3 command, then refocus. i3 hands focus to a client on workspace switches.
- [x11] Wait for the i3 `RUN_COMMAND` reply before querying again. Otherwise the tree read can precede the command's effect.
