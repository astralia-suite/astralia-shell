# `astralia-shell` index

## Rule

- One-line, no break.
- Grouped by `directory`, one `##` heading per directory.
- Entry format: `file`: Purpose (≤ 20 words).
- Reflect current structure and function of each file in the code base.
- No mentions of past fixes.

## `/`

- `meson.build`: Builds the core libraries, the `astralia` executable, the backend plugins and the tests.
- `meson.options`: `backends` selects the plugins to build; `native_cpu` tunes the x11 plugin for the X201.
- `build.sh`: Sets up dependencies per backend, then builds, tests, installs, runs or uninstalls the shell.
- `readme.md`: Overview, features, requirements, install, build options, usage, configuration and testing.
- `.clang-format`: Project code style.
- `.gitignore`: Ignores the build tree, caches and `local/`.
- `CLAUDE.md`: Project instructions: restrictions, markdown, planning and convention rules.
- `LICENSE`: The project license text.
- `THIRD_PARTY_LICENSES`: Licenses of the bundled third-party code and assets.

## `important/`

- `convention.md`: Commenting, formatting, backend and module boundary, and include rules.
- `knowledge.md`: Programming techniques and pitfalls, one statement and one explanation each, tagged by backend where they differ.
- `index.md`: This map of the code base.

## `assets/`

- `stellar-restoration.{svg,png}`: The screensaver logo as vector and raster; only the `svg` is installed.
- `stiletto.svg`: Sprite the stiletto rain draws.

## `assets/bamboo/`

- `bamboo.png`: Icon of the bamboo window.
- `astralia-shell-bamboo.desktop.in`: Desktop entry template for the bamboo window.

## `assets/constellation/`

- `C1.png` to `C6.png`: Launcher row bullets, one per visible slot.

## `assets/fonts/`

- `tabler-icons.ttf`: The icon font behind every glyph in `src/render/icons.h`.
- `ComicShannsMono-Regular.otf`: Bundled monospace font.
- `YujiMai.ttf`: Bundled Japanese display font.

## `assets/logout/`

- `logo.png`: Logo drawn at the centre of the logout ring.

## `assets/pam/`

- `astralia-shell`: PAM service the lock screen authenticates against.

## `assets/polkit/`

- `electro.png`: Password echo glyph for the polkit, lock and network dialogs.

## `assets/wallpaper/`

- `default.jpg`: Default wallpaper and the first theme source.

## `assets/shaders/`

- `NOTICE`: Attribution for third-party shader code.

## `assets/shaders/launcher/`

- `thunder_rect_shock.frag`: Rectangular shock burst of the launcher entrance.

## `assets/shaders/logout/`

- `thunder_shock.frag`: Ring shock of the logout strike.

## `assets/shaders/renderer/`

- `quad.vert`: Unit-quad vertex shader that shapes and textures share.
- `polygon.vert`, `polygon_tex.vert`: Vertex shaders for polygons, plain and textured.
- `rect.frag`, `rrect.frag`: Fragment shaders for rectangles and rounded rectangles with borders.
- `fillet.frag`: Fragment shader for corner fillets.
- `tex.frag`, `rounded_tex.frag`: Fragment shaders for textures, plain and rounded.
- `video.frag`: Fragment shader for video frames.

## `assets/shaders/thunder/`

- `thunder_burst.frag`: Bolt and ring burst of the thunder strike.

## `assets/shaders/visualizer/`

- `fullscreen.vert`: Fullscreen vertex shader that emits only `gl_Position`.
- `audio_pass.frag`, `audio_smooth.frag`, `audio_average.frag`, `audio_gravity.frag`: Audio texture passes: copy, smoothing, averaging and gravity.

## `assets/shaders/visualizer/bar/`

- `bar.frag`: Bar spectrum shader.

## `assets/shaders/visualizer/sphere/`

- `*.glsl`: Sphere shader parts: common code, heads, mains, defaults and `pnoise`.
- `sphere1_splat.frag`, `present.vert`, `present.frag`: Splat accumulation pass and the present pass of the sphere.

## `assets/shaders/wallpaper/`

- `*.frag`: Wallpaper transitions `fade`, `wipe`, `zoom`, `stripes`, `disc` and `honeycomb`.

## `src/`

- `main.cpp`: Parses the invocation, answers `modules`, runs the IPC client, loads the session's backend, takes the lock and runs.

## `src/app/`

- `backend.h`: The `Backend` interface every plugin implements and the name of its exported factory symbol.
- `session.{h,cpp}`: Session detection from the environment and the `dlopen` loader that finds `libastralia-<backend>.so`.
- `shell.{h,cpp}`: `Capabilities` per backend, the `ShellVerb` table, verb bindings, open-module tracking, `status` and the module report.

## `src/backend/wayland/`

- `backend.cpp`: Wayland backend plugin: services, modules and the poll loop, run as `Backend::run`.
- `meson.build`: Wayland source list, protocol generation, dependencies, compile arguments, media plugin sources and shader installation.

## `src/backend/wayland/app/`

- `wayland_state.h`: `WaylandState`, the process-wide Wayland state every module and service receives.
- `wayland_registry.{h,cpp}`: Registry binding, EGL bootstrap and renderer initialisation.
- `config.{h,cpp}`: Wayland adapter over the shared `Config`: type aliases and per-output accessors.
- `module.h`: The `Module` interface of single-instance overlays.
- `per_monitor_module.h`: The `PerMonitorModule` interface of modules with one surface per output.
- `module_registry.{h,cpp}`: Builds the modules a `Capabilities` set allows.
- `monitor_output.{h,cpp}`: `MonitorOutput`: one output's surfaces, modules and their lifecycle.
- `service.h`: The `Service` interface: init, timer tick and poll sources.
- `service_registry.{h,cpp}`: Builds the Wayland services.
- `key_dispatch.{h,cpp}`: Routes key events to the open module.
- `ipc.{h,cpp}`: Collects every module's verb bindings for the IPC server.
- `user_info.{h,cpp}`: Username, OS name, uptime text and profile picture path.

## `src/backend/wayland/config/`

- `idle_overlay_config.h`: Idle overlay fade time, animation owner ids and layer namespace.
- `visualizer_gl_config.h`: Visualizer GL constants and aliases of the shared parameter types.

## `src/backend/wayland/core/`

- `poll_source.{h,cpp}`: `PollSource` and `FnPollSource`, the poll-loop extension points.
- `log.{h,cpp}`: `klog` forwarding to the shared log.

## `src/backend/wayland/plugin/`

- `media_plugin.{h,cpp}`: The `ffmpeg` decoder, built as its own plugin and loaded by the media service.

## `src/backend/wayland/protocols/`

- `wlr-layer-shell-unstable-v1.xml`: Layer-shell protocol.
- `wlr-foreign-toplevel-management-unstable-v1.xml`: Foreign toplevel management protocol.
- `hyprland-toplevel-export-v1.xml`: Hyprland toplevel export protocol.
- `text-input-unstable-v3.xml`: Text-input protocol.

## `src/backend/wayland/render/`

- `renderer.{h,cpp}`: The GLES2 `Renderer`: frame setup, clip, opacity, transform and shape draws.
- `gl_canvas.{h,cpp}`: `GlCanvas`, the immediate-mode `ui::Canvas` over the renderer.
- `gl.{h,cpp}`: Shader compile and load, error checks and EGL context make-current helpers.
- `texture.{h,cpp}`: `Texture` and its RGBA uploads, plus extension probes.
- `texture_cache.{h,cpp}`: `TextureCache` keyed by content with eviction.
- `text.{h,cpp}`: Rasterises text with Pango into RGBA and textures.
- `icon.{h,cpp}`: Rasterises icon and display glyphs.
- `image.{h,cpp}`: Loads image files into textures.
- `arc_gauge.{h,cpp}`: Cached arc gauge textures.
- `video_texture.{h,cpp}`: Imports decoded `DMA-BUF` frames as textures.
- `wallpaper_column.{h,cpp}`: Draws a wallpaper column and its transitions.
- `thunder.{h,cpp}`: The bolt, ring and rectangular shock burst shaders.
- `layer_surface.{h,cpp}`: Layer-shell surface configuration and teardown.
- `overlay_panel.{h,cpp}`: `OverlayPanelBase`: surface, input region, toggle and frame requests of a full overlay.
- `popup_window.{h,cpp}`: `PopupWindowBase`: `xdg_popup` creation, repositioning and frames.
- `toplevel_window.{h,cpp}`: `ToplevelWindowBase`: `xdg_toplevel` surface lifecycle and frames.
- `marquee_scroll.h`: Marquee scrolling over the Wayland animations.

## `src/backend/x11/`

- `backend.cpp`: X11 backend plugin: connects to X, builds the services and every module, binds the verbs and runs the loop.
- `meson.build`: X11 source list, dependencies, compile arguments and asset installation.

## `src/backend/x11/app/`

- `services.{h,cpp}`: The services this backend owns, built from the shared services, the compositor and the telemetry state.
- `active_target.{h,cpp}`: The output an overlay opens on: the widget's output, else the focused monitor, else the pointer's.

## `src/backend/x11/config/`

- `bar_config.h`: The bar styles X11 builds and the allocator trim interval.

## `src/backend/x11/core/`

- `x_connection.{h,cpp}`: `XConnection` with atoms, outputs and cursors.
- `event_loop.{h,cpp}`: `EventLoop`: X event dispatch added to a `PollReactor`.
- `keyboard.{h,cpp}`: Key symbols and modifiers from X key events.
- `pointer.{h,cpp}`: Pointer, scroll and key event conversion to the neutral input types.
- `x_clipboard.{h,cpp}`: The `CLIPBOARD` selection owner behind the `Clipboard` interface.

## `src/backend/x11/render/`

- `cairo_canvas.{h,cpp}`: `CairoCanvas`, the `ui::Canvas` over a cairo context.
- `draw.{h,cpp}`: Cairo colour and rounded-rectangle helpers.
- `text.{h,cpp}`: Pango text layout with wrapping and ellipsis.
- `image_decode.{h,cpp}`: Cairo image surfaces over the shared decoder, opacity test and JPEG writing.
- `app_icon.{h,cpp}`: Loads application icons into cairo surfaces.
- `cover_cache.{h,cpp}`: On-disk cache names and expiry for cover images.
- `wallpaper_paint.{h,cpp}`: Paints a per-output wallpaper and computes its signature.
- `x_window.{h,cpp}`: `XWindow`: creation, mapping, managed and click-through modes.

## `src/config/`

- `bar_layout.h`: `BarStyle` enum, bar height, margins, pill gaps, workspace, dock and peek constants shared by both bars.
- `panel_config.h`: Panel card, scroll, row, slider, typography and per-panel constants.
- `visualizer_config.h`: Visualizer constants and the `VisualizerParams` stored in the config.
- `rain_config.h`: Rain constants and the `RainParams` stored in the config.
- `bamboo_config.h`: Bamboo window size, timing, character grid, growth constants and colours.
- `lock_config.h`: Lock screen card, font, dot, background and asset constants and the layer namespace.
- `idle_config.h`: Idle activity pulse and screensaver logo constants both hosts use.
- `dashboard_config.h`: Dashboard window title, id and default size.
- `launcher_config.h`: Launcher commands, search URLs and limits, row, bullet and animation geometry.
- `notification_config.h`: Notification card geometry, timing and urgency colours.
- `polkit_config.h`: Polkit card geometry, line heights, dot size, echo asset and texts.
- `logout_config.h`: Logout button ring geometry, angles, logo asset and the action table.
- `osd_config.h`: OSD size, margins and display timing.
- `overview_config.h`: Overview grid size, scale and rounding.
- `settings_config.h`: Settings window geometry, tab labels and row sizes.
- `wallpaper_config.h`: Desktop context menu width, padding, row height and text sizes.
- `wallpaper_select_config.h`: Wallpaper selector card scale, gap, image sizes, label geometry and motion timings.

## `src/core/`

- `reactor.h`: Abstract fd, timer and poll-source interface that services use instead of a concrete loop.
- `poll_reactor.{h,cpp}`: `poll()` and `timerfd` reactor with `SIGINT` and `SIGTERM` handling.
- `animation.{h,cpp}`: `AnimationManager`, easings and the instant flag that makes X11 snap to end values.
- `input.h`: Neutral key, pointer and scroll events every backend converts into.
- `clipboard.h`: The backend-neutral `Clipboard` interface: publish text and fetch it asynchronously.
- `log.{h,cpp}`: Timestamped logging to stderr and the state log, plus crash and terminate handlers.
- `json.{h,cpp}`: Minimal JSON value, parser and writer.
- `config_file.{h,cpp}`: `key = value` file parsing, atomic write, home expansion and a directory `FileWatch`.
- `ipc.{h,cpp}`: Unix socket IPC server with verb dispatch, and the one-shot client.
- `cli.{h,cpp}`: Picks daemon, foreground or IPC client mode from the arguments.
- `runtime_paths.{h,cpp}`: Per-session lock, socket and log paths named from `WAYLAND_DISPLAY` or `DISPLAY`.
- `single_instance.{h,cpp}`: `flock`-based single-instance guard.
- `daemon.{h,cpp}`: Detaches the process into the background.
- `async_process.{h,cpp}`: Runs a child with `posix_spawn` and reports its output on the reactor thread.
- `spawn.{h,cpp}`: Launches detached shell commands with a double fork.
- `dbus.{h,cpp}`: `sdbus` connection driven by the reactor, with proxy and property helpers.
- `deferred_call.{h,cpp}`: Runs closures posted from any thread on the reactor thread.
- `path_home.{h,cpp}`: Collapses and expands `~` in paths.
- `allocator.{h,cpp}`: Caps `malloc` arenas and pins the `mmap` threshold.
- `wall_clock.{h,cpp}`: Local wall time from the precise system clock, and the wait until the next second boundary.
- `signal.h`: Typed signal and slot publisher.
- `unique_fd.h`: Owning file descriptor.

## `src/render/`

- `canvas.h`: The `Canvas` interface every view draws through: shapes, fillets, erase mode, text, images, groups, opacity and gauges.
- `tokens.h`: `Color`, the mutable palette (with the sticker roles `border_outer`, `border_inner` and `shadow`), metrics and colour helpers shared by every backend.
- `oklab.h`: OKLab and OKLCH conversion with gamut reduction, WCAG contrast and hue distance.
- `palette_extract.{h,cpp}`: Downsamples an image, clusters it with k-means in OKLab and ranks up to four seeds.
- `theme.{h,cpp}`: `Theme` roles derived from the seeds in OKLCH for the light and dark schemes, and `theme_apply` onto the palette; `ink` is the deep line colour and `text` the reading colour.
- `sticker.{h,cpp}`: `sticker()`, the five-role look every component shares (text, fill, inner ring, outer border, hard offset shadow), and `selectable()`, the shared control look.
- `scroll.{h,cpp}`: Scroll geometry, painting and slide-in offsets, horizontal or vertical; the rollers touch when closed and float a gap off the paper when open.
- `scroll_sequence.{h,cpp}`: `ScrollSequence`, the staged slide, unroll and content fade of every scroll.
- `icons.h`: The Tabler icon glyph codepoints used by every backend.
- `glyphs.h`: Volume and brightness glyph thresholds over the icon table.
- `geometry.h`: `Box`, the one rectangle type.
- `host.h`: The surface and host contract a backend gives a module.
- `module_base.{h,cpp}`: Module base class with config fan-out and IPC registration over a host.
- `text_field.{h,cpp}`: Text field state, key handling, UTF-8 cursor and selection editing, type animation and row slide.
- `field_view.{h,cpp}`: Draws a text field with its caret, selection and animated characters over a canvas.
- `marquee.{h,cpp}`: Scrolling text for labels wider than their box.
- `decode.{h,cpp}`: The one image decoder: `stb_image`, `resvg` and reduced-size `libjpeg` into an RGBA buffer.
- `app_fonts.{h,cpp}`: Registers the bundled fonts with fontconfig and holds the unhinted icon font options.

## `src/service/`

- `settings_service.{h,cpp}`: The unified `Config`, its JSON load and save, per-output override accessors and hot reload.
- `wallpaper_service.{h,cpp}`: Resolves and edits wallpaper image, column count and fill mode per output, and lists folders.
- `theme_service.{h,cpp}`: Themes from the fixed Keqing default palette, or from the wallpaper or the first animated frame, off-thread after the first sync.
- `battery_service.{h,cpp}`: UPower display-device status, device list and `on_battery`, read asynchronously.
- `bluetooth_service.{h,cpp}`: BlueZ adapter and device state with coalesced async refresh, device kinds and `rfkill` control.
- `network_service.{h,cpp}`: NetworkManager status, `nmcli` scans while watched, ethernet state and connectivity checks.
- `notification_service.{h,cpp}`: Owns the notification D-Bus name; urgency, per-notification timeouts and close reasons.
- `polkit_service.{h,cpp}`: Authentication agent with request details, prompt, echo flag and a ready signal.
- `tray_service.{h,cpp}`: StatusNotifier watcher with item status, menus and icon path resolution.
- `media_service.{h,cpp}`: MPRIS player selection, track metadata and playback control.
- `audio_service.{h,cpp}`: PipeWire sink, source and stream levels with route-aware volume writes.
- `brightness_service.{h,cpp}`: Backlight level through `brightnessctl`, with a `sysfs` watch and a 1% floor.
- `icon_service.{h,cpp}`: Resolves icon-theme and window-class icon file paths.
- `telemetry_service.{h,cpp}`: CPU and GPU temperature, usage, clock and system statistics from `sysfs` and `/proc`.
- `compositor_service.{h,cpp}`: The `Compositor` interface, shared state types, work-area maths and `make_compositor`.
- `hyprland_service.{h,cpp}`: Hyprland compositor over its request and event sockets, with Lua dispatch, workspace paging and palette-driven window border colours.
- `i3_service.{h,cpp}`: i3 compositor over the i3 IPC socket, with event subscription and a layout-derived window tree.
- `dock_service.{h,cpp}`: Per-output dock entries from the compositor client list, ordered left to right.
- `user_service.{h,cpp}`: Display name, OS name, uptime text and the profile picture path.

## `src/service/wayland/`

- `output_service.{h,cpp}`: Wayland outputs, their geometry and per-surface scale tracking.
- `input_service.{h,cpp}`: Seat keyboard, pointer click, scroll and capability state, converted into neutral input events.
- `text_input_service.{h,cpp}`: `text-input-v3` client for preedit and committed input method text.
- `clipboard_service.{h,cpp}`: Data-device clipboard behind the `Clipboard` interface.
- `idle_service.{h,cpp}`: `ext-idle-notify` state and per-monitor activity times.
- `frame_service.{h,cpp}`: `FrameClock` and `request_frame`, the frame-callback pacing of a surface.
- `capture_service.{h,cpp}`: Hyprland toplevel export captures for the overview.
- `media_service.{h,cpp}`: Animated wallpaper decode through the media plugin, frame callbacks and `DMA-BUF` planes.
- `settings_service.{h,cpp}`: Saves the `Config` from the Wayland side.

## `src/service/x11/`

- `output_service.{h,cpp}`: RandR outputs and the output under the pointer.

## `src/modules/bamboo/`

- `grow.{h,cpp}`: ASCII bamboo with `│` culms on a character grid, drawn into an ARGB buffer: growth, hold, fade, regrow.
- `random.{h,cpp}`: The seedable random source of the bamboo.
- `frame.h`: The view of the bamboo buffer.

## `src/modules/bamboo/wayland/`

- `bamboo.{h,cpp}`: Managed bamboo window that uploads the buffer on a timer.

## `src/modules/bamboo/x11/`

- `bamboo.{h,cpp}`: Managed bamboo window that blits the buffer on a timer.

## `src/modules/bar/model/`

- `model.{h,cpp}`: `bar::Model` state: items (each with its own `retracts` flag), refresh, hover, pinning, linger, volume peek, press and hit tests.
- `layout.cpp`: `Model::layout`, one zone-driven layout for every style; the style only supplies data (zones, dividers, workspace badge size, icon gap).
- `motion.cpp`: Workspace pill growth and the animated dock row.
- `items.{h,cpp}`: The one `Item` to `PanelId` mapping and back, shared by both backends.
- `zones.{h,cpp}`: Left, centre and right slot lists a style points at (`bar_zones`, shared by every style).
- `item.h`: The `Item` enum.

## `src/modules/bar/paint/`

- `style.{h,cpp}`: `bar::StyleSpec` for the three styles (`continuous`, `okinami`, `pop`), the `pop` fallback, island, fillet and autohide geometry.
- `frame.{h,cpp}`: The bar background over the canvas for both backends: pill or rail and islands, then fillets.
- `view.{h,cpp}`: Paints items, workspaces, dock icons and dividers over the canvas.
- `chips.{h,cpp}`: Shared painters: pill with ring, icon and label chip, two-line chip, numbered badge, icon row.

## `src/modules/bar/panel/core/`

- `panel.{h,cpp}`: `Panel` scroll card with staged open, content scrolling, drag, dialogs and a refresh timer.
- `panel_set.{h,cpp}`: Owns the panels, keeps one open at a time and routes changes to the host.
- `widgets.{h,cpp}`: Sliders, toggles, device rows, flat bars and the confirm dialog the panels share.

## `src/modules/bar/panel/content/`

- `battery_panel.{h,cpp}`: Battery panel content.
- `bluetooth_panel.{h,cpp}`: Bluetooth panel content.
- `brightness_panel.{h,cpp}`: Brightness panel content.
- `date_panel.{h,cpp}`: Date and calendar panel content.
- `time_panel.{h,cpp}`: Time panel content: big time, seconds, AM/PM and timezone, repainted every second.
- `media_panel.{h,cpp}`: Media player panel content.
- `network_panel.{h,cpp}`: Network panel content with the password dialog.
- `resource_panel.{h,cpp}`: CPU, GPU and memory panel content.
- `tray_panel.{h,cpp}`: Tray panel content.
- `volume_panel.{h,cpp}`: Volume panel content.

## `src/modules/bar/wayland/`

- `bar.{h,cpp}`: Per-monitor bar surface.
- `panel_surface.{h,cpp}`: The one overlay `PanelSurface` per monitor that hosts the open panel.

## `src/modules/bar/x11/`

- `bar.{h,cpp}`: Bar window, struts and autohide strip.
- `bar_set.{h,cpp}`: `BarSet`, one bar per output.
- `panel_host.{h,cpp}`: The one `PanelHost` window that hosts the open panel.

## `src/modules/dashboard/wayland/`

- `dashboard.{h,cpp}`: Managed dashboard window.

## `src/modules/dashboard/x11/`

- `dashboard.{h,cpp}`: Managed dashboard window drawn with cairo.

## `src/modules/idle/`

- `bounce.{h,cpp}`: Bouncing screensaver logo step shared by both hosts.
- `clock.{h,cpp}`: Per-output activity clock, ambient and screensaver verdicts and the policy-change test.

## `src/modules/idle/wayland/`

- `idle.{h,cpp}`: Per-monitor ambient and screensaver overlays driven by `ext-idle-notify`.

## `src/modules/idle/x11/`

- `idle.{h,cpp}`: Polls `MIT-SCREEN-SAVER` idle time and shows a click-through overlay per output.

## `src/modules/launcher/`

- `model.{h,cpp}`: Query with caret, selection and clipboard keys, mode, debounce, async search, results and submenu.
- `view.{h,cpp}`: Paints the search field, mode bullets, result rows and highlight, with an optional entrance frame.
- `entrance.{h,cpp}`: Pure lightning entrance: outline trace, sky bolt, card snap-in, rectangular burst and backdrop dim.
- `search.{h,cpp}`: Mode detection, query parsing and URL building.
- `apps.{h,cpp}`: Scores installed apps against a query.
- `files.{h,cpp}`: Walks and scores files below a search root.
- `desktop_entry.{h,cpp}`: Parses desktop entries and scans application directories.
- `launch.{h,cpp}`: Builds and runs launch commands.
- `submenu.{h,cpp}`: Desktop actions of an app as a submenu.
- `visits.{h,cpp}`: Remembers visited results to rank them.

## `src/modules/launcher/wayland/`

- `launcher.{h,cpp}`: Launcher overlay host with the lightning entrance shaders.

## `src/modules/launcher/x11/`

- `launcher.{h,cpp}`: Launcher window host painted through cairo.

## `src/modules/lock/`

- `model.{h,cpp}`: Password field, authentication generation, failure timeout, hit boxes and the per-output `lock::Motion`.
- `view.{h,cpp}`: Paints the battery, system, media, clock, avatar, password, resources and notification cards.
- `layout.{h,cpp}`: Card, column and password dot geometry and the development unlock corner shared by both hosts.
- `pam.{h,cpp}`: Blocking PAM check for a user and password against a service directory.
- `slash.{h,cpp}`: Display-free `SlashPlan` sampling the thunder-slash transition: shards, cutmarks, strikes, flash and timeline.
- `transition_view.{h,cpp}`: Paints the lock and unlock slash transitions over the canvas, with bolts, rays and flash.

## `src/modules/lock/wayland/`

- `lock.{h,cpp}`: `ext-session-lock` host that paints the shared view per output with its typing motion.
- `transition.{h,cpp}`: Plays the slash transition on transparent overlay surfaces before the lock and after a correct password.
- `wallpaper.{h,cpp}`: Loads and draws the per-output lock wallpaper.

## `src/modules/lock/x11/`

- `lock.{h,cpp}`: Override-redirect window per output, input grabs, hotplug, cached backdrop and the PAM thread.

## `src/modules/logout/`

- `model.{h,cpp}`: Selection, hover, commands and the open and close choreography.
- `layout.{h,cpp}`: Button positions on the logout ring and hit testing.
- `view.{h,cpp}`: Paints the ring, buttons and logo over the canvas.

## `src/modules/logout/wayland/`

- `logout.{h,cpp}`: Logout overlay host with the thunder strike shaders.
- `logout_style_config.h`: Constants only the Wayland logout host draws with.

## `src/modules/logout/x11/`

- `logout.{h,cpp}`: Logout window host painted through cairo.

## `src/modules/notification/`

- `model.{h,cpp}`: Entries, slide and fade, countdown and the layout of the stack.
- `view.{h,cpp}`: Paints the notification stack over the canvas.

## `src/modules/notification/wayland/`

- `notification.{h,cpp}`: Click-through notification surface with an input region for the close boxes.
- `notification_style_config.h`: Constants only the Wayland notification host draws with.

## `src/modules/notification/x11/`

- `notification.{h,cpp}`: Notification windows with a `SHAPE` input region.

## `src/modules/osd/`

- `model.{h,cpp}`: Kind, level, glyph, fade and hide deadline of the on-screen display.
- `view.{h,cpp}`: Paints the on-screen display over the canvas.

## `src/modules/osd/wayland/`

- `osd.{h,cpp}`: On-screen display layer surface.
- `osd_style_config.h`: Constants only the Wayland OSD host draws with.

## `src/modules/osd/x11/`

- `osd.{h,cpp}`: On-screen display window.

## `src/modules/overview/`

- `model.{h,cpp}`: Block layout over monitors, tiles, paging, indicator, drag and drop, keys and the scroll stages.
- `view.{h,cpp}`: Paints the scroll, tiles, workspace numbers, indicator and the live capture hook.
- `paging.{h,cpp}`: Workspace paging maths for the overview grid: workspace at a cell, page of a workspace, arrow stepping.

## `src/modules/overview/wayland/`

- `overview.{h,cpp}`: Overview overlay host with live toplevel captures.

## `src/modules/overview/x11/`

- `overview.{h,cpp}`: Overview window host painted through cairo.

## `src/modules/polkit/`

- `model.{h,cpp}`: Authentication prompt state, password field, dots and error text.
- `layout.{h,cpp}`: Authentication card height, visible password dots and UTF-8 length.
- `view.{h,cpp}`: Paints the authentication card and echo dots over the canvas.

## `src/modules/polkit/wayland/`

- `polkit.{h,cpp}`: Authentication overlay host with exclusive keyboard input.
- `polkit_style_config.h`: Constants only the Wayland polkit host draws with.

## `src/modules/polkit/x11/`

- `polkit.{h,cpp}`: Authentication window host painted through cairo.

## `src/modules/rain/`

- `matrix.{h,cpp}`: CPU matrix rain simulation into an ARGB buffer.
- `stiletto.{h,cpp}`: CPU stiletto rain simulation into an ARGB buffer.
- `random.{h,cpp}`: The seedable random source of the rain.
- `frame.h`: The view of the rain buffer.

## `src/modules/rain/wayland/`

- `rain.{h,cpp}`: Managed rain window that uploads the buffer.

## `src/modules/rain/x11/`

- `rain.{h,cpp}`: Managed rain window drawn with cairo.

## `src/modules/settings/`

- `model.{h,cpp}`: Tile menu, tabs by capability (Color Palette always last) opened as pages with a back key, monitor selections, fields, pickers and actions.
- `view.{h,cpp}`: Paints the scroll, the tile menu with live summaries, each tab page, fields, pickers and thumbnails.
- `pane.h`: `Pane`, the faded drawing helpers every settings page paints through.
- `wallpaper_columns.{h,cpp}`: Wallpaper column chips and the per-monitor column panel.

## `src/modules/settings/wayland/`

- `settings.{h,cpp}`: Settings overlay host.

## `src/modules/settings/x11/`

- `settings.{h,cpp}`: Settings window host painted through cairo.

## `src/modules/visualizer/`

- `audio_capture.{h,cpp}`: PipeWire capture of the sink into overlapping left and right fragments.
- `fft.{h,cpp}`: The windowed radix-2 FFT that turns a fragment into spectrum bins.
- `cpu_stages.{h,cpp}`: CPU peak hold, gravity, averaging and smoothing of the spectrum, as the X11 host runs it.
- `bars.{h,cpp}`: Bar count, spectrum texel and height for the bar shape.

## `src/modules/visualizer/wayland/`

- `visualizer.{h,cpp}`: Visualizer window on its own render thread with a share context.
- `audio_stages.{h,cpp}`: GPU smoothing, averaging and gravity passes over the audio textures.
- `bars.{h,cpp}`: `BarVisualizer`, the bar shape renderer.
- `sphere.{h,cpp}`: `SphereVisualizer`, the sphere renderer.
- `shaders.{h,cpp}`: Assembles the sphere shader sources from the installed parts.

## `src/modules/visualizer/x11/`

- `visualizer.{h,cpp}`: Managed visualizer window drawn with cairo.

## `src/modules/wallpaper/`

- `model.{h,cpp}`: `DesktopMenuModel`, the right-click menu: placement clamped into the output, entries, hover and hit tests.
- `view.{h,cpp}`: Paints the desktop menu over the canvas at a given origin.

## `src/modules/wallpaper/wayland/`

- `wallpaper.{h,cpp}`: Per-output background surface; a right-click toggles the menu drawn on that surface.

## `src/modules/wallpaper/x11/`

- `wallpaper.{h,cpp}`: Root pixmap painter and a bottom `InputOnly` desktop window per output for the right-click menu.

## `src/modules/wallpaper_select/`

- `model.{h,cpp}`: Image or video listing, selection, wheel, zoom and step tweens, card fade, apply handoff and hit tests.
- `view.{h,cpp}`: Paints the current wallpaper as a card with neighbours, label and hit boxes.

## `src/modules/wallpaper_select/wayland/`

- `wallpaper_select.{h,cpp}`: Full-output overlay with exclusive keyboard, retargeted to the output it opens on.

## `src/modules/wallpaper_select/x11/`

- `wallpaper_select.{h,cpp}`: Override-redirect window with keyboard focus, painted through cairo.

## `test/`

- `main.cpp`: Plain check runner with an isolated `XDG_STATE_HOME`; returns non-zero on failure.
- `check.h`: `test::check` helper and failure counter.
- `palette_fixture.h`: Fixed palette that tests install before painting.
- `backend_smoke.sh`: Starts each backend through the real executable and stops it over IPC.

## `test/app/`

- `test_session.cpp`: Session detection, backend library names and search directories.
- `test_shell.cpp`: The verb table, capabilities and the module report.

## `test/backend/wayland/`

- `test_wayland.cpp`: Runs the Wayland unit checks as `wayland-unit`.

## `test/backend/wayland/app/`

- `test_config.cpp`: Config parsing, config watching and per-monitor overrides.

## `test/backend/wayland/core/`

- `test_poll_source.cpp`: Poll-source dispatch.

## `test/backend/wayland/render/`

- `test_image_decode.cpp`: Image decoding of PNG, SVG, JPEG and truncated files.
- `test_marquee_scroll.cpp`: Marquee scroll animation.
- `test_media_decode.cpp`: Media service decode and frame handling.
- `test_palette.cpp`: Palette tokens as the Wayland renderer sees them.

## `test/backend/wayland/service/`

- `test_active_output.cpp`: Active output choice in the Wayland output service.
- `test_dock.cpp`: Dock entries from the compositor client list.
- `test_input_convert.cpp`: Key and pointer conversion to the neutral events.
- `test_keyboard.cpp`: Keyboard state in the Wayland input service.

## `test/backend/x11/`

- `test_x11.cpp`: Checks for the X11 pure logic: colours, the cairo frame, image placement and decoding and input conversion.

## `test/core/`

- `pump.h`: Runs a `PollReactor` until a condition holds or a limit passes.
- `test_animation.cpp`: `AnimationManager` and easings.
- `test_async_process.cpp`: Async child output and cancellation.
- `test_config_file.cpp`: Config file parsing, atomic writes and the directory watch.
- `test_deferred_call.cpp`: Closures posted across threads.
- `test_input.cpp`: Neutral input events.
- `test_invocation.cpp`: Command-line mode selection.
- `test_json.cpp`: JSON parsing and writing.
- `test_path_home.cpp`: Collapsing and expanding `~`.
- `test_reactor.cpp`: Timers, fds and poll sources of the reactor.
- `test_spawn.cpp`: Detached spawning.

## `test/modules/bamboo/`

- `test_grow.cpp`: Bamboo growth, hold, fade and regrow.

## `test/modules/bar/`

- `test_bar.cpp`: Bar styles, glyphs, text, layout, dock slide, workspace slots, interaction and frame.
- `test_panel.cpp`: Panel scroll card, dialogs and content.

## `test/modules/idle/`

- `test_idle.cpp`: Idle clock verdicts and the bouncing logo.

## `test/modules/launcher/`

- `test_entrance.cpp`: Lightning entrance sampling.
- `test_launcher.cpp`: Query text, modes, scoring, listing, desktop entries, URLs and visit store.
- `test_model.cpp`: Launcher model keys, search and results.

## `test/modules/lock/`

- `test_layout.cpp`: Lock card and dot geometry.
- `test_lock.cpp`: Lock model, view and authentication flow.
- `test_slash.cpp`: Slash transition sampling.

## `test/modules/logout/`

- `test_layout.cpp`: Ring positions and hit testing.
- `test_logout.cpp`: Logout model and view.

## `test/modules/notification/`

- `test_notification.cpp`: Notification model and view.

## `test/modules/osd/`

- `test_osd.cpp`: OSD model and view.

## `test/modules/overview/`

- `test_overview.cpp`: Overview model and view.
- `test_paging.cpp`: Workspace paging maths.

## `test/modules/polkit/`

- `test_layout.cpp`: Authentication card height and visible dots.
- `test_polkit.cpp`: Polkit model and view.

## `test/modules/rain/`

- `test_rain_buffer.cpp`: Rain simulation into its buffer.

## `test/modules/settings/`

- `test_settings.cpp`: Settings model and view.

## `test/modules/visualizer/`

- `test_cpu_stages.cpp`: CPU spectrum stages.
- `test_fft.cpp`: FFT bins.

## `test/modules/wallpaper/`

- `test_menu.cpp`: Desktop menu placement, hover and hit tests.

## `test/modules/wallpaper_select/`

- `test_wallpaper_select.cpp`: Wallpaper selector model and view.

## `test/render/`

- `recording_canvas.h`: A canvas that records draw calls so views are checked headless.
- `test_module_base.cpp`: Module base config fan-out and IPC registration.
- `test_oklab.cpp`: OKLab conversion and contrast.
- `test_palette_extract.cpp`: Clustering and seed ranking.
- `test_scroll.cpp`: Scroll geometry.
- `test_scroll_sequence.cpp`: Scroll stage timing.
- `test_text_cursor.cpp`: Text field cursor and selection editing.
- `test_theme.cpp`: Theme roles and `theme_apply`.
- `test_tokens.cpp`: Colour tokens and helpers.

## `test/service/`

- `fake_compositor.h`: A `Compositor` stand-in for service tests.
- `test_compositor.cpp`: Compositor state and work-area maths.
- `test_services.cpp`: Parsing and decision logic of the shared services.
- `test_settings.cpp`: Config load, save and overrides.
- `test_telemetry.cpp`: Telemetry readings and sensor selection.
- `test_theme_service.cpp`: Theme service sync and generations.
- `test_wallpaper.cpp`: Wallpaper resolution per output.
