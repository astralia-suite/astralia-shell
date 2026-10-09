# Merge tasks

Plan: `merge-plan.md` in this directory. Baselines: `audit.md`.

## Phase 0: audit and baseline

- [x] Baseline `hl`: optimized binary size and build time (idle RSS pending a Hyprland session).
- [x] Baseline `i3`: binary size and idle RSS (build time not captured).
- [x] Parser comparison: `nlohmann_json` vs in-house `json.cpp` (`clients` sample is synthetic).
- [x] Write `audit.md`: module split, service pairs, duplicated core files, IPC verbs (partial), tests.
- [x] Review gate: user approved proceeding to Phase 1.

## Phase 1: shared core

- [x] `reactor.h` and `poll_reactor` added; `async_process`, `ipc`, `dbus`, `config_file` take `Reactor &`.
- [x] Core files copied into `src/core/` under `namespace astralia`; `deferred_call`, `path_home` ported from `hl`.
- [x] `log` merged: `i3` API, `hl` timestamps, state file and crash handler.
- [x] `json` gains `write_json`.
- [x] `runtime_paths` names from `WAYLAND_DISPLAY` or `DISPLAY`; prefix is `astralia`.
- [x] `tune_allocator(int)` takes the arena count; `daemonize()` sends output to `/dev/null`.
- [x] Core tests ported or added; `./build.sh test` passes with no warnings.
- [x] `important/index.md`, `knowledge.md`, `convention.md` updated.
- [ ] Review gate: user reviews Phase 1 before Phase 2.

## Phase 2: shared services

- [x] Display-free `i3` services imported onto `Reactor`: `audio`, `battery`, `bluetooth`, `brightness`, `media`, `network`, `notification`, `polkit`, `tray`, `user`.
- [x] `core/dbus` gains `dbus_get_all_async`; battery, bluetooth and network read D-Bus state without blocking.
- [x] `battery`: `hl` device list and `on_battery` merged in.
- [x] `bluetooth`: device kinds, addresses, scanning state, `rfkill`, coalesced async refresh.
- [x] `network`: ethernet device status, active connectivity check, async status chain, watch only while the panel is open.
- [x] `notification`: urgency, per-notification timeouts, earliest-deadline timer.
- [x] `polkit`: request details, prompt, echo flag, ready signal.
- [x] `tray`: item status and secondary activate; `icon_service` and `telemetry_service` added; `user` gains the OS name.
- [x] `settings_service` holds the unified JSON `Config` with legacy `settings.conf` and `wallpaper.conf` import; `wallpaper_service` resolves from it.
- [x] Tests ported or added for every service's pure logic; real-system smoke runs done for battery, bluetooth, network, telemetry and config.
- [x] `dock_service` landed in Phase 3.
- [ ] Review gate: user reviews Phase 2 before Phase 3.

## Phase 3: compositor interface

- [x] `compositor_service.h`: `Compositor` interface, shared state types, `CloseScope`, `make_compositor`.
- [x] `hyprland_service`: port of `hl`, in-house JSON, events on the reactor, opt-in client polling, socket timeouts kept.
- [x] `i3_service`: i3 IPC state, event subscription, layout-derived window geometry, global coordinates, workspace operations.
- [x] `dock_service` ported; tests for dock, Hyprland parsing, events and commands, i3 tree, framing and state.
- [x] Live checks: Hyprland read and workspace round trip in the user's session; i3 state, events and every operation in a nested `Xephyr`.
- [ ] Review gate: user reviews Phase 3 before Phase 4.

## Phase 4: backend interface and loader

- [x] `app/backend.h`: `Backend` interface and the exported factory symbol `astralia_backend_create`.
- [x] `app/session.{h,cpp}`: detection (override, Hyprland signature, `I3SOCK`, `XDG_SESSION_TYPE`, display variables) and the `dlopen` loader with a search path.
- [x] `main.cpp`: invocation, client, detection, load, lock, daemonize, allocator tuning, run.
- [x] `meson.build` and `meson.options`: core linked whole and exported, two backend plugins, `backends` and `native_cpu` options.
- [x] Stub backends serve the IPC socket; `test/backend_smoke.sh` starts and stops each through the real executable.
- [x] Checked in this session: debug and daemon mode run the wayland backend, `help` and `kill` work, the executable links no display library.
- [ ] Review gate: user reviews Phase 4 before Phase 5.

## Phase 5: move backends

### X11

- [x] `i3` render, modules, config, X core and assets copied into `src/backend/x11/` and `assets/`; backend include root added.
- [x] `EventLoop` rebuilt on `PollReactor`; `Services` rebuilt from the shared services and `make_compositor`.
- [x] Modules moved to the shared APIs: workspace row, dock and overview from the compositor, settings tabs and wallpaper from `Config`, shared icon resolution.
- [x] `x11-unit` test target with the ported `i3` pure-logic checks plus `workspace_status`; the stale terminal-wrapper check fixed.
- [x] Backend smoke test starts the real x11 backend nested in `Xephyr`.
- [x] Run nested in `Xephyr` with the real modules: bar, settings, launcher, overview, workspace pills and dock follow real windows and workspace switches.
- [ ] Not done: `i3`'s `knowledge.md` entries are not merged yet (Phase 7); unused legacy constants remain in `settings_config.h` and `wallpaper_config.h`.

### Wayland

- [x] `hl` source, protocols, shaders, gifs and assets imported into `src/backend/wayland/` with `wayland/`-prefixed includes; `nlohmann` removed (config over the shared JSON, Hyprland parsing over the shared parsers).
- [x] `main` turned into `WaylandBackend::run`; IPC socket and lock follow the shared paths; media plugin built as its own module.
- [x] `wayland-unit` target with all 38 `hl` tests; backend smoke test checks the plugin loads and fails cleanly without a display.
- [x] Run live in the Hyprland session: all services connect, launcher, logout, dashboard, overview and settings toggle, exit code 0, no errors. Idle RSS 89 MB, 114 MB after the overlays.
- [x] Migrated to the shared services and verified live: config type, compositor and dock, IPC server, `PollReactor` loop, telemetry, icons, brightness, media, audio, battery.
- [x] Migrated and verified: network, bluetooth, tray, notification (`post` added to the shared service), polkit, wallpaper resolve, user info; launcher search moved to the callback `AsyncProcess`.
- [x] `hl` copies of `async_process`, `deferred_call`, `path_home` and their tests deleted; `klog` forwards to the shared log; superseded service copies deleted.
- [ ] Review gate: user reviews Phase 5 before Phase 6. (Work is done and live-checked; only the review is open.)

## Phase 6: share module models

- [x] `launcher`: the seven logic files, the model types and the search constants moved to `src/modules/launcher/` and `src/config/launcher_config.h`; both backends use them; `SearchProcess` replaced by `AsyncProcess` in `x11`; submenu icons are the `SubmenuIcon` enum.
- [x] `launcher` behaviour settled: configurable terminal for terminal apps, `cd ... && exec` for terminal in a directory, dead `Back` and `OpenContainingDir` launch cases dropped, visit store path unchanged.
- [x] `overview`: workspace paging maths shared in `src/modules/overview/paging.{h,cpp}`, grid constants in `src/config/overview_config.h`.
- [x] `polkit`: card geometry, texts and layout maths shared; `logout`: ring geometry, action table and hit testing shared; `osd`: size, margins and timing shared.
- [x] `icons.h`: the Tabler glyph table is one shared header.
- [x] Left per backend on purpose: `notification` (card design differs), `settings` (tab set and drawing differ), bar styles (different spec model), `wallpaper` (the service holds the logic), `lock`, `idle`, `dashboard`, `rain`, `visualizer`.
- [x] Tests: shared launcher, paging, polkit and logout checks under `test/modules/`; headless launcher search in `wayland-unit`; `./build.sh test` passes with no warnings.
- [x] Live checks: x11 nested in `Xephyr` (launcher search and submenu, logout ring, overview stepping); Wayland in Hyprland (launcher search and open and close, logout, overview, settings toggles, clean exit).
- [ ] Review gate: user reviews Phase 6.

## Phase 7: finish

- [x] `important/index.md`, `convention.md` updated; `knowledge.md` now holds the shared entries plus the `hl` entries tagged `[wayland]` and the `i3` entries tagged `[x11]`.
- [x] `build.sh`: `ASTRALIA_BACKENDS` and `ASTRALIA_NATIVE_CPU` map to the meson options.
- [x] `readme.md` written.
- [ ] Not done: size and idle RSS against the Phase 0 baseline for the `x11` backend (see `progress.md`).

## Single module directory

- [x] Every module's backend views moved into `src/modules/<name>/wayland/` and `x11/`; backend-only services moved into `src/service/wayland/` and `src/service/x11/`; module-only configs moved next to their views; module tests moved into `test/modules/<name>/`.
- [x] `./build.sh test` passes with no warnings after the move.
- [ ] Not done: the `wayland/service/*.h` alias headers remain; no controller logic is shared yet.

## Phase 8: shared contracts

- [x] `core/input.h`: neutral key, pointer and scroll events in `astralia::input`; Wayland converts in `input_service`, X11 in `core/pointer.{h,cpp}`. Modules are not switched yet; each switches in its merge phase.
- [x] `AnimationManager`, `Easing` and the instant flag moved to `core/animation`; `wayland/render/animation.h` is an alias header until Phase 13. X11 calls `animation_set_instant(true)`, so only Wayland animates.
- [x] `ui/tokens.h` replaces both `palette.h`, which are now alias headers; `ui/glyphs.h` holds the volume and brightness icon thresholds, used by both `render/icons.h` wrappers.
- [x] `ui/host.h` (`Host`, `Surface`, `OutputInfo`, `FocusMode`) and `ui/module_base.{h,cpp}` (`ModuleBase`, `ModuleContext`, `register_ipc`), covered by a fake host test.
- [x] Gate: `./build.sh test` passes four tests with no warnings.

## Phase 9: canvas and the two-module spike

- [x] `ui/canvas.h` (`Canvas`, `TextStyle`, `FontFamily::{text,icon,glyph}`, groups with scale and clip, images), `GlCanvas` over `Scene` with a `TextureCache` and a pooled colour arena, `CairoCanvas` over `cairo` and pango.
- [x] Shared `core/`-level pieces the spike needed: `ui/text_field` (state, keys, type animation), `ui/marquee`, `ui/geometry.h`, colour helpers in `ui/tokens.h`.
- [x] `osd` and `polkit` merged: `OsdModel`/`paint_osd` and `PolkitModel`/`paint_polkit`, headless tests with a recording canvas, both old per-backend copies deleted. Visible changes: the polkit message uses the 12 px font on both backends; Wayland dots pop from their centre and show the last characters when the field is full; the Wayland osd takes a whole percent.
- [x] Budgets measured on both backends (see `merge-plan.md`, "Phase 9 result").
- [x] Gate: go on one view per module.

## Phase 10: overlays

- [x] `logout`: `LogoutModel` (selection, hover, the whole open and close choreography on `AnimationManager`, commands per backend) and `paint_logout` over the canvas, with a logo painter hook. The Wayland host keeps the layer surface, the animated logo and the thunder shaders; the X11 host keeps the window, the logo load and `malloc_trim`. On X11 the instant mode collapses the choreography to its final state. Visible changes: the X11 lock button has no command (lock is Wayland-only), the Wayland glyphs rasterise at the output scale.
- [x] `notification`: `NotificationModel` (entries, slide and fade, countdown), `NotificationViewState` (local close fade, hover), `layout_notifications` and `paint_notifications` over the canvas; one `notification_config.h`. Visible changes: both backends wrap long text and use the urgency dot, the countdown bar (Wayland only, it needs animation) and the glyph close button; the X11 close hit area is 40 px; the X11 cards no longer draw a hand-made X; fonts are pixel sizes 17, 23 and 20.
- [x] `wallpaper`: decided to stay per backend. X11 paints once into the root pixmap with the cover decode and has no window, frame loop or canvas; Wayland runs live GL surfaces with video, animated images and transitions; the shared part, picking the image by monitor and column, already lives in `wallpaper_service`.
- [x] Tests: `test/modules/logout/test_logout.cpp`, `test/modules/notification/test_notification.cpp`; the X11 notification layout checks moved there. `./build.sh test` passes with no warnings.
- [x] Live: Wayland in Hyprland (logout opens and closes, buttons render, a notification card with wrapped text), X11 nested in `Xephyr` (logout ring and keys, notification stack with urgency colours, polkit prompt with typed dots and cancel).

## Phase 11: launcher, overview and settings

- [x] `launcher`: `LauncherModel` (query, mode, debounce on the reactor, async search, results, selection, scroll and highlight offsets, submenu, input method edits) and `paint_launcher` over the canvas; `ui/field_view` shared with the settings fields. Visible changes: the X11 launcher shows the live blink caret hooks and the Wayland height, highlight and scroll animations through the instant mode only on Wayland; the search no longer needs `LauncherState`.
- [x] `overview`: `OverviewModel` (one layout function with the single monitor as the one-block case, global mode on both backends, tiles with animated rectangles, indicator, slide, drag and drop, every key of the old Wayland overview) and `paint_overview` with a tile-art hook for the live Wayland captures; `compositor_work_area` now handles scale and transform. Visible changes: X11 gains the global view and the shift and alt modifiers; the workspace numbers use the 40 px font on both; placeholder tiles use the `surface_alt` fill and a centred icon, the Wayland corner icon appears only on live captures.
- [x] `settings`: `SettingsModel` (tabs by capability, monitor selections, fields with edit and commit, wallpaper pickers with threaded scans, every action of the old tabs) and `paint_settings` over the canvas, with `Canvas::thumbnail` for async thumbnails on both backends. Capabilities: Wayland adds the animation, idle, logout, rain and visualizer tabs, animated wallpaper, columns and the default-wallpaper toggle; X11 has the bar, displays and wallpaper tabs, a per-output bar toggle and no autohide. Visible changes: one wallpaper tab on both (directory field, rescan, remove, filename labels); X11 settings is a full-output window like the other overlays; Up and Down switch tabs on both.
- [x] Tests: `test_model` for the launcher, `test_overview`, `test_settings`; the X11 layout checks for notification, overview and settings were replaced by them. `./build.sh test` passes with no warnings.
- [x] Live: X11 nested in `Xephyr` (launcher search with icons and selection, overview with a window tile and keys, settings tabs and wallpaper scrolling); Wayland in Hyprland (launcher, overview and settings toggles, no errors).

## Phase 12: bar

- [x] `BarStyleSpec` (`modules/bar/style.{h,cpp}`): one spec for `islands`, `okinami` and `continuous`, `bar_style_resolve` maps `islands` to `continuous` in one place when the backend does not build it (X11), plus the shared island and fillet geometry (`bar_frame`) and `bar_autohide_geometry`. Each backend keeps only its frame painter: `modules/bar/wayland/frame.{h,cpp}` (scene nodes with punch, fillet and hug textures) and `modules/bar/x11/frame.{h,cpp}` (cairo).
- [x] `BarModel` (`modules/bar/model.{h,cpp}`) and `paint_bar` (`view.{h,cpp}`): items (logout, tray, resource, network, bluetooth, volume, brightness, battery, media, clock) with glyph, label, visibility and an expand tween, the workspace row (`workspace_slots` makes X11 show ten pills from any output, Wayland shows what the compositor reports), the dock row with its reorder tween, hover, pinning by an open panel, the close linger, the volume peek, layout in three groups, dividers, island spans, hit testing and `BarAction`. Both old widget and style trees are deleted.
- [x] Panels (`modules/bar/panel/`): `Panel` (reveal, scroll, drag, dialogs, refresh timer), `PanelSet` (one open at a time), and the contents `battery`, `bluetooth`, `brightness`, `clock`, `media`, `network`, `tray`, `volume` plus the Wayland-only `resource` over the new `Canvas::gauge`. Hosts: `PanelSurface` (one overlay layer surface per monitor instead of nine, IME through `TextInputClient`) and the X11 `PanelHost` (one window with a pointer grab). Both per-backend panel trees, their styles and chrome helpers are deleted; the bar sources went from about 10,300 lines to about 5,800.
- [x] Visible changes: the tray menu is a card under the tray card with a back row instead of a popup window; the network password dialog has no blinking caret and no marquee on long names; the X11 panels anchor and size like the Wayland ones (400 px cards, the clock 504 px centred, the media panel centred); X11 pills expand with the pointer through the instant mode and show the volume peek; the X11 inactive workspace pill is dim text instead of 20% alpha; one clock format and the Wayland label wording (`muted`, `Disconnected`, `Sign in`); panels open on a per-monitor `astralia panel-<name>` verb (new on Wayland); the X11 bar redraws the whole bar once a second instead of the clock rectangle.
- [x] Tests: `test/modules/bar/test_bar.cpp` (styles, frame, autohide geometry, glyphs and labels, layout for continuous and okinami, workspace slots, hover, pinning, linger, hit tests, painting) and `test_panel.cpp` (the panel framework, the calendar, the tray menu rows, the network rows); the X11 widget, calendar and slider checks moved or went with their code. `./build.sh test` passes with no warnings.
- [x] Live: Wayland in Hyprland (all nine panels through the new verbs, the three styles through a scratch `XDG_CONFIG_HOME`), X11 nested in `Xephyr` (bar, hover, the network, volume, bluetooth and media panels, Escape).
- [ ] Not done: the per-monitor bar host contract shared by `BarSet` and `PerMonitorModule` waits for `app/shell` in Phase 13; the Wayland autohide and hug radius paths are only exercised by the geometry tests.

## Phase 13: glue and services

- [x] `app/shell.{h,cpp}`: one `ShellVerb` table with names, descriptions and the capability each verb needs, `Capabilities` per backend (`wayland_capabilities`, `x11_capabilities`), `Shell::bind` (refused when the backend lacks the capability), `after_verb`, `track` for open modules, `attach` to the `IpcServer`, and a built-in `status` verb that prints the backend, its capabilities and the open modules. Both backends bind their verbs on it: the Wayland `IpcHandler` adapter and the X11 per-module `ipc.add` calls are gone. X11 gains the `panel-*` verbs and Wayland gains `panel-*`, both from the shared table.
- [x] Decision: the Wayland `Module` and `PerMonitorModule` registries and `dispatch_key_events` stay. They are the surface and EGL wiring and the focus routing that the plan lists as permanently per backend, and X11 has no equivalent because its key events arrive on the window that owns them. Only the verb, capability and open-state glue moved to the shell.
- [x] Alias headers removed: `wayland/service/*.h`, `wayland/render/{animation,palette,color_ops,rect,icons}.h`, `wayland/core/{deferred_call,path_home}.h` and the X11 `render/{palette,icons}.h`; about 150 call sites now use `astralia::` names, `BrightnessBackend` and the pipewire helper wrappers are replaced by direct `BrightnessService` and `AudioService` calls, and the dead Wayland render helpers (`dock_row`, `marquee_text`, `panel_scroll`, `popup_window`, `progress_bar`, `slider`) are deleted. The brightness service is now created before the surfaces.
- [x] Compositor and backend are chosen independently: `SWAYSOCK` selects the Wayland backend (Sway also exports `I3SOCK`), `I3Compositor` reads `I3SOCK` then `SWAYSOCK`, and the i3 tree parser names native Wayland windows by `app_id`. Work-area maths with scale and transform is `compositor_work_area` since Phase 11 and the workspace pills are one model since Phase 12.
- [x] Tests: `test/app/test_shell.cpp`, the Sway cases in `test_session.cpp` and `test_compositor.cpp`; `./build.sh test` passes with no warnings.
- [x] Live: Wayland in Hyprland (`help`, `status`, the panel verbs and the overlay verbs), X11 nested in `Xephyr` (`help` and `panel-volume`).
- [ ] Not verified: a real Sway session, which this machine cannot run.

## Phase 14: capabilities and finish

- [x] `Capabilities` (`lock`, `idle`, `dashboard`, `rain`, `visualizer`, `animated_wallpaper`, `resource_panel`, `animations`) per backend in `app/shell`; the Wayland registries build the dashboard, rain, visualizer, lock and idle modules only for flags the backend reports, `SettingsCaps` derives its overlapping fields from them, and `astralia modules` prints the module and verb tables that `readme.md` embeds.
- [x] Size: the core is split into `astralia-core-base` (`app/`, `core/`, linked whole into the executable and exported) and `astralia-core-shared` (services, `ui/`, modules, linked statically into each plugin so only used objects stay); plugin code is built with hidden visibility, `-ffunction-sections`, `-fdata-sections` and `--gc-sections`, and exports only `astralia_backend_create`. Executable plus plugin, unstripped: Wayland 3,578,760 B against 3,877,920 B before the spike and 3,248,552 B for `hl`; X11 3,046,480 B against 3,244,816 B before the spike and 2,282,248 B for `i3`. The remaining excess over the old projects is accepted because idle RSS is far below the baseline.
- [x] Budgets (build tree, five minutes of toggling every panel on both backends): idle RSS at 7 s Wayland 111,976 KB against 182,364 KB, X11 26,400 KB against 29,552 KB; after the loop Wayland 116,176 KB (growth stops once the 512-entry texture cache is full), X11 26,936 KB; idle CPU over 30 s Wayland 0.37%, X11 0.03% after the clock redraw only runs while its label is shown.
- [x] Docs: `index.md` rewritten for the merged tree, `convention.md` updated for the core split, the module layers and capabilities, `knowledge.md` cut to the 20-word rule with the entries of deleted code removed and the merge lessons added, `readme.md` carries the module and verb tables.
- [x] `./build.sh test` passes four tests with no warnings; `clang-format` was run over `src/` and `test/`.

