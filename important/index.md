# `astralia-shell` index

## Rule

- One-line, no break.
- Grouped by `directory`, one `##` heading per directory.
- Entry format: `file`: Purpose (≤ 20 words).
- Reflect current structure and function of each file in the code base.
- No mentions of past fixes.

## `/`

- `meson.build`: Builds `astralia-core-base`, `astralia-core-shared`, the `astralia` executable, the backend plugins and the tests.
- `meson.options`: `backends` selects which backend plugins build; `native_cpu` tunes the x11 plugin for the X201.
- `assets/`: Fonts, constellation bullets, logos, shaders, gifs, the PAM service and the polkit glyph, installed to `share/astralia-shell/`.
- `build.sh`: Sets up dependencies per backend, builds, tests, installs, runs or uninstalls the shell.
- `readme.md`: Requirements, install, build options, running, backend selection and the module and verb tables.
- `.clang-format`: Project code style.

## `important/`

- `convention.md`: Commenting, formatting, source layout, backend and module boundaries, config, build and include rules.
- `knowledge.md`: Hard-won development rules, one statement and one explanation each, tagged by backend where they differ.
- `handoff.md`: Merge status, architecture in brief, budgets, open items, rendering position and how to resume.
- `index.md`: This map of the code base.

## `src/`

- `main.cpp`: Parses the invocation, answers `modules`, runs the IPC client, loads the session's backend, takes the lock and runs.

## `src/app/`

- `backend.h`: The `Backend` interface every plugin implements and the name of its exported factory symbol.
- `session.{h,cpp}`: Session detection from the environment and the `dlopen` loader that finds `libastralia-<backend>.so`.
- `shell.{h,cpp}`: `Capabilities` per backend, the `ShellVerb` table, verb bindings, open-module tracking, `status` and the module report.

## `src/backend/wayland/`

- `backend.cpp`: Wayland backend plugin: services, modules and the poll loop, run as `Backend::run`.
- `meson.build`: Wayland source list, protocol generation, dependencies, compile arguments, media plugin sources and asset installation.
- `app/`: `WaylandState`, registry binding, monitor outputs, module and service registries, key dispatch, shell bindings and the config adapter.
- `core/`: Poll-source types for the run loop and `klog` forwarding to the shared log.
- `config/`: Wayland configs that more than one module uses: idle, rain and visualizer parameters.
- `render/`: The GLES2 renderer, scene graph, `GlCanvas`, text, icons, layer-shell windows, toplevel windows and the arc gauge.
- `plugin/media_plugin.cpp`: The `ffmpeg` decoder, built as its own plugin and loaded by the media service.
- `protocols/`: Protocol XML that the system `wayland-protocols` package does not ship.

## `src/backend/x11/`

- `backend.cpp`: X11 backend plugin: connects to X, builds the services and every module, binds the verbs and runs the loop.
- `meson.build`: X11 source list, dependencies, compile arguments and asset installation.
- `app/services.{h,cpp}`: The services this backend owns, built from the shared services and the compositor.
- `core/`: `XConnection`, `Keyboard`, pointer conversion and the `EventLoop` that adds X event dispatch to a `PollReactor`.
- `render/`: `CairoCanvas`, cairo drawing, text, icons, image decoding and X windows shared by the modules.
- `config/bar_config.h`: The bar styles X11 builds and the allocator trim interval.

## `src/core/`

- `reactor.h`: Abstract fd, timer and poll-source interface that services use instead of a concrete loop.
- `poll_reactor.{h,cpp}`: `poll()` and `timerfd` reactor with `SIGINT` and `SIGTERM` handling; backends extend it through poll sources.
- `animation.{h,cpp}`: `AnimationManager`, easings and the instant flag that makes X11 snap to end values.
- `input.h`: Neutral key, pointer and scroll events every backend converts into.
- `log.{h,cpp}`: Timestamped logging to stderr and `~/.local/state/astralia/astralia.log`, plus crash and terminate handlers.
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
- `signal.h`: Typed signal and slot publisher.
- `unique_fd.h`: Owning file descriptor.

## `src/render/`

- `canvas.h`: The `Canvas` interface every view draws through: shapes, text, images, groups, opacity and gauges.
- `tokens.h`: `Color`, the palette, metrics and colour helpers shared by every backend.
- `glyphs.h`: Volume and brightness glyph thresholds over the icon table.
- `geometry.h`: `Box`, the one rectangle type.
- `host.h`: The surface and host contract a backend gives a module.
- `module_base.{h,cpp}`: Module base class with config fan-out and IPC registration over a host.
- `text_field.{h,cpp}`: Text field state, key handling, type animation and row slide.
- `field_view.{h,cpp}`: Draws a text field with its caret and animated characters over a canvas.
- `marquee.{h,cpp}`: Scrolling text for labels wider than their box.

## `src/config/`

- `bar_style.h`: `BarStyle` enum with its names and labels.
- `bar_layout.h`: Bar height, margins, pill gaps, workspace, dock and peek constants shared by both bars.
- `panel_config.h`: Panel card, row, slider, typography and per-panel constants.
- `visualizer_config.h`: Visualizer constants and the `VisualizerParams` stored in the config.
- `rain_config.h`: Rain constants and the `RainParams` stored in the config.
- `launcher_config.h`: Launcher commands, search URLs and limits, row and animation geometry.
- `notification_config.h`: Notification card geometry, timing and urgency colours.
- `polkit_config.h`: Polkit card geometry, line heights, dot size and texts.
- `logout_config.h`: Logout button ring geometry, angles and the action table.
- `osd_config.h`: OSD size, margins and display timing.
- `overview_config.h`: Overview grid size, scale and rounding.
- `settings_config.h`: Settings window geometry, tab labels and row sizes.
- `icons.h`: The Tabler icon glyph codepoints used by every backend.

## `src/modules/`

- `<name>/`: One directory per shell part: shared model, view and logic at the top, backend hosts in `wayland/` and `x11/`.
- `<name>/model.{h,cpp}`: Display-free state, logic and animation targets, driven by neutral events and tested headless.
- `<name>/view.{h,cpp}`: The one view, painted over `ui::Canvas` for both backends.
- `<name>/<backend>/`: The backend's host: surfaces, input conversion, timers and the module-only style config.
- `bar`, `launcher`, `lock`, `logout`, `notification`, `osd`, `overview`, `polkit`, `settings`, `wallpaper`: Have a host in both backends.
- `idle`, `dashboard`, `rain`, `visualizer`: Have a `wayland/` host only, gated by `Capabilities`.

## `src/modules/bar/`

- `style.{h,cpp}`: `BarStyleSpec` for the three styles, the `islands` fallback, island and fillet geometry, autohide geometry.
- `model.{h,cpp}`: Items, workspace pills, dock row, hover, pinning, linger, volume peek, layout and hit tests.
- `view.{h,cpp}`: Paints items, workspaces, dock icons and dividers over the canvas.
- `panel/panel.{h,cpp}`: `Panel` card with reveal, scroll, drag, dialogs and a refresh timer, plus the `PanelContent` interface.
- `panel/panel_set.{h,cpp}`: Owns the panels, keeps one open at a time and routes changes to the host.
- `panel/widgets.{h,cpp}`: Sliders, toggles, device rows, flat bars and the confirm dialog the panels share.
- `panel/*_panel.{h,cpp}`: Content of the battery, bluetooth, brightness, clock, media, network, resource, tray and volume panels.
- `wayland/`: Per-monitor bar surface, one overlay `PanelSurface`, and the scene-node frame with fillets and hug corners.
- `x11/`: Bar window and struts, `BarSet` per output, one `PanelHost` window, and the cairo frame painter.

## `src/modules/lock/`

- `model.{h,cpp}`: Password field, authentication generation, failure timeout and button hit boxes.
- `view.{h,cpp}`: Paints the battery, system, media, clock, avatar, password, resources and notification cards over the canvas.
- `layout.{h,cpp}`: Card, column and password dot geometry shared by both hosts.
- `pam_authenticator.{h,cpp}`: Blocking PAM check for a user and password against a service directory.
- `wayland/`: `ext-session-lock` host with its own animated scene-graph card, kept for the GL renderer.
- `x11/`: Override-redirect window per output, keyboard and pointer grabs, wallpaper backdrop and the PAM thread.

## `src/modules/launcher/`

- `model.{h,cpp}`: Query, mode, debounce, async search, results, selection, scroll, submenu and input method edits.
- `view.{h,cpp}`: Paints the search field, mode bullets, result rows and highlight.
- `search.{h,cpp}`: Mode detection, query parsing and URL building.
- `apps_provider.{h,cpp}`: Scores installed apps against a query.
- `files_provider.{h,cpp}`: Walks and scores files below a search root.
- `desktop_entry.{h,cpp}`: Parses desktop entries and scans application directories.
- `launch_action.{h,cpp}`: Builds and runs launch commands.
- `submenu.{h,cpp}`: Desktop actions of an app as a submenu.
- `visit_store.{h,cpp}`: Remembers visited results to rank them.

## `src/modules/overview/`

- `model.{h,cpp}`: Block layout over one or many monitors, tiles, paging, indicator, drag and drop and keys.
- `view.{h,cpp}`: Paints tiles, workspace numbers, indicator and the live capture hook.
- `paging.{h,cpp}`: Workspace paging maths for the overview grid: workspace at a cell, page of a workspace, arrow stepping.

## `src/modules/settings/`

- `model.{h,cpp}`: Tabs by capability, monitor selections, fields, wallpaper pickers and every settings action.
- `view.{h,cpp}`: Paints each tab, fields, pickers and thumbnails over the canvas.

## `src/modules/logout/`, `notification/`, `osd/`, `polkit/`

- `logout/model.{h,cpp}`: Selection, hover, commands and the open and close choreography.
- `logout/layout.{h,cpp}`: Button positions on the logout ring and hit testing.
- `notification/model.{h,cpp}`: Entries, slide and fade, countdown and the layout of the stack.
- `osd/model.{h,cpp}`: Kind, level, glyph, fade and hide deadline of the on-screen display.
- `polkit/model.{h,cpp}`: Authentication prompt state, password field, dots and error text.
- `polkit/layout.{h,cpp}`: Authentication card height, visible password dots and UTF-8 length.
- `*/view.{h,cpp}`: The one view of each module, painted over the canvas.

## `src/service/`

- `settings_service.{h,cpp}`: The unified `Config`, its JSON load and save, per-output override accessors, legacy import and hot reload.
- `wallpaper_service.{h,cpp}`: Resolves and edits wallpaper image, column count and fill mode per output from `Config`.
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
- `hyprland_service.{h,cpp}`: Hyprland compositor over its request and event sockets, with Lua dispatch commands and workspace paging.
- `i3_service.{h,cpp}`: i3 and Sway compositor over the i3 IPC socket, with event subscription and a layout-derived window tree.
- `dock_service.{h,cpp}`: Per-output dock entries from the compositor client list, ordered left to right.
- `user_service.{h,cpp}`: Display name, OS name and uptime text.
- `wayland/`: Wayland-only services: `capture`, `frame`, `idle`, `input`, `media` decoder, `output`, settings editing and `text_input`.
- `x11/output_service.{h,cpp}`: RandR outputs and the output under the pointer.

## `test/`

- `main.cpp`: Plain check runner with an isolated `XDG_STATE_HOME`; returns non-zero on failure.
- `check.h`: `test::check` helper and failure counter.
- `render/recording_canvas.h`: A canvas that records draw calls so views are checked headless.
- `core/pump.h`: Runs a `PollReactor` until a condition holds or a limit passes.
- `backend_smoke.sh`: Starts each backend through the real executable and stops it over IPC.
- `backend/wayland/`: The Wayland unit tests that are not module tests, run as `wayland-unit`.
- `backend/x11/test_x11.cpp`: Checks for the X11 pure logic: colours, the cairo frame, image cover decoding and input conversion.
- `modules/<name>/`: Checks for each module's model, view and layout; `<backend>/` holds host-level checks.
- `app/`: Checks for session detection, library names, search directories, the Sway session and the shell.
- `core/test_*.cpp`: Checks for the `src/core/` files.
- `service/test_*.cpp`: Checks for the pure parsing and decision logic of the shared services.
