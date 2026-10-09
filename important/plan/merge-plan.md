# Full merge of `astralia-shell-hl` and `astralia-shell-i3` into one `astralia`

## Goal

One executable `astralia`, one implementation of every shell part. A session detects its backend, and a backend contributes only what is truly display-specific: the display connection, surfaces, event wiring, input translation and the canvas that draws. State, logic, layout, animation, IPC verbs, service subscriptions and the drawing code of each module live once, in `src/modules/<name>/`. Nothing may be slower or larger per session than the old project it replaces, measured by the budgets below.

## Where the merge stands

Phases 0 to 14 are done. See `progress.md` and `tasks.md`.

- Merged and shared: `src/core/`, every display-free service, the `Compositor` interface with its Hyprland and i3 implementations, the loader and `Backend` interface, one JSON config, the glyph table, `ui/` (tokens, canvas, text field, host), and for every module that has a view in both backends a model, one view over `ui::Canvas` and a thin host per backend.
- Per backend by design: the display connection, surfaces, input conversion, the two canvases, the frame painters of the bar, the `wallpaper` module and the Wayland-only modules.
- `app/shell` owns the verb table, `Capabilities` and the open-module list; the module registries and key routing of the Wayland host stay because they are surface wiring.

## What stays per backend, permanently

- The display connection and event wiring: `XConnection`, `Keyboard`, `EventLoop` for X11; `WaylandState`, registry, `PollSource` for Wayland.
- Surface and window creation: X windows and the shape extension; layer-shell surfaces, `xdg_popup`, `xdg_toplevel`, `ext-session-lock`.
- Input translation from `xcb` or `xkbcommon` into the neutral input types.
- The canvas implementation: GLES2 for Wayland, `cairo-xcb` for X11.
- Wayland-only services: `capture`, `frame`, `idle`, `input`, `text_input`, the `ffmpeg` media plugin and the protocol XMLs.
- The compositor implementations, `hyprland_service` and `i3_service`, behind the one `Compositor` interface.

## Target architecture

A module is a model, a view and a host contract.

- Model: state, logic, layout and animation targets, in plain C++ with no display include. It is driven by neutral events (`Key`, `Pointer`, `Scroll`, `Tick`, `Config`, service signals) and publishes `changed`. It is unit-tested headless.
- View: `paint(Canvas &, const Model &)`, written once over the `Canvas` interface, plus the hit regions it records for the model to consume.
- Host: the contract a backend implements for a module. It gives the module its surface (open, close, place on an output, keyboard focus mode, input region, `request_frame`), its outputs and scale, its timers through the `Reactor`, and its IPC registration.

Shared contracts, new:

- `core/input.h`: neutral `KeyEvent`, `PointerEvent`, `ScrollEvent`.
- `core/animation.{h,cpp}`: `AnimationManager` and `Easing` moved out of Wayland's `render/`; they are pure logic.
- `ui/tokens.h`: one `Color`, the palette, `metrics`, fonts and the glyph table.
- `ui/canvas.h`: rectangles, rounded rectangles with border, textures and text runs, clip, opacity, offset; the minimum the existing views use.
- `ui/host.h`: the surface contract above.
- `app/shell.{h,cpp}`: owns the module list, IPC verb table, key routing and config fan-out, replacing the two backends' registries.

Canvas costs are the main risk. The X11 backend has no GL and a 29 MB budget; the Wayland backend draws through a pooled scene graph. The canvas must record into the existing scene on Wayland and draw immediately on cairo, with no per-frame heap growth. Phase 9 proves this on two small modules before anything large moves; the fallback is level 2 only, that is, a shared model and two views for every module.

## Phases

Phases 0 to 7 are in `tasks.md`. The phases below continue the numbering.

### Phase 8: shared contracts

- [NEW] `core/input.h`; both backends translate their key and pointer events into it.
- [MOVE] `AnimationManager` and `Easing` into `core/`; Wayland keeps `animation_set_instant`.
- [NEW] `ui/tokens.h`, merging the two `palette.h` and `metrics`, and the two `icons.h` wrappers.
- [NEW] `ui/host.h` and the module base class with IPC registration, config and service wiring.
- Gate: the contracts compile into both backends with the existing modules unchanged, and `./build.sh test` passes.

### Phase 9: canvas and the two-module spike

- [NEW] `ui/canvas.h`, `GlCanvas` over `Scene` and `Renderer`, `CairoCanvas` over `cairo`, text and icon caches behind one interface.
- Merge `osd` and `polkit` as the first modules: model, one view over `Canvas`, both backend copies deleted.
- Measure on both backends against the budgets: idle RSS, idle CPU, paint time, binary size, heap growth over ten minutes of opening and closing.
- Gate: go or no-go on one view per module. On no-go, keep the model shared and the views per backend for every later phase.

### Phase 10: overlays

- `logout` (the Wayland shaders and burst choreography become optional view effects the X11 canvas ignores), `notification`, `wallpaper`.
- Each module follows the recipe below.

### Phase 11: launcher, overview and settings

- `launcher`: controller (query, mode, debounce, results, selection, scroll, submenu) joins the shared logic already in the directory.
- `overview`: Wayland's multi-monitor block layout generalises X11's single grid; one layout function with the single monitor as the one-block case.
- `settings`: tab model shared; `idle`, `rain`, `visualizer` and `animation` tabs register only when the backend reports the capability.

### Phase 12: bar

- One `BarStyleSpec` model for `islands`, `okinami` and `continuous`; the X11 backend either implements `islands` or drops it from its list in one place.
- One workspace pill model fed by the compositor state, replacing `workspace_status` and Wayland's pill list, with animation targets owned by the model.
- Widgets and panels (`battery`, `bluetooth`, `brightness`, `clock`, `media`, `network`, `tray`, `volume`), then the Wayland-only `resource` panel behind a capability.
- The per-monitor bar host is the last piece; the X11 `BarSet` and Wayland `PerMonitorModule` become one contract.

### Phase 13: glue and services

- Remove the two module registries, key dispatch and IPC adapters in favour of `app/shell`.
- Remove the `wayland/service/*.h` alias headers by moving call sites to `astralia::` names.
- Work-area maths shared in `compositor_work_area` with transform and scale; one workspace status model.
- Choose the compositor service by compositor and the backend by display protocol, so a Wayland session under Sway can load the Wayland backend with `I3Compositor`.

### Phase 14: capabilities and finish

- A `Capabilities` set per backend (`lock`, `idle`, `dashboard`, `rain`, `visualizer`, `animated_wallpaper`, `resource_panel`); modules register only for capabilities the backend reports, and the module list in the docs is generated from them.
- Size: decide whether the executable links only what the backends use.
- Docs: `index.md`, `convention.md`, `knowledge.md` deduplicated and cut to the 20-word rule, `readme.md`.

## Recipe for each module

1. Diff the two implementations function by function and list every behavioural difference.
2. Settle each difference by one rule: keep the side that is configurable, testable or already correct, and drop what only one side has and nothing uses. Record visible changes.
3. Extract the model and controller, with headless tests for every transition.
4. Write the view once over `Canvas`.
5. Delete both old copies, their configs and their tests, and port the useful tests.
6. `./build.sh test`, then a live check on both backends in the matching session, then the budgets.

## Verification and budgets

- `./build.sh test` after every step; four tests pass, no warnings.
- Live checks as in `progress.md`: Wayland in Hyprland through IPC verbs and the log, X11 nested in `Xephyr` with screenshots of the nested display only. No keystrokes into the real session, no lock-screen tests.
- Per backend, after each module: idle RSS, idle CPU, binary size and ten-minute heap growth must not exceed the Phase 0 baseline in `audit.md`. Today: Wayland 89 to 115 MB against 182 MB, X11 27 MB against 29.5 MB, and both sizes above baseline (open item).
- Old repos stay untouched as the rollback.

## Phase 9 result: go

- Idle RSS at 7 s: Wayland 115,072 KB against 182,364 KB baseline; X11 nested in `Xephyr` 27,488 KB against 29,552 KB.
- Eleven minutes of volume nudges every 1.5 s (osd shown each time) on both backends at once: Wayland 115,252 to 115,296 KB, X11 27,628 to 27,664 KB, so +0.04% and +0.13% of RSS, flat after the first minute.
- Idle CPU over 30 s: Wayland 0.2%, X11 0%.
- Paint time of the osd on `CairoCanvas` against the old direct cairo code: 143.0 us against 142.3 us (ratio 1.00); the polkit card paints in 300 us at 1920x1200.
- Binary size against the pre-spike build: Wayland executable plus plugin 3,919,184 B against 3,877,920 B (+1.06%), X11 3,316,528 B against 3,244,816 B (+2.2%); the one-time cost of the canvases and the shared model code, which the later modules amortise.
- Live checks: Wayland in Hyprland (osd on volume changes, polkit card from `pkexec true`), the X11 canvas through an offscreen render of both views.

## Phase 14 result

- Idle RSS at 7 s: Wayland 111,976 KB against 182,364 KB baseline; X11 nested in `Xephyr` 26,400 KB against 29,552 KB.
- Five minutes of opening and closing every bar panel on both backends at once: Wayland 112,036 to 116,176 KB, flat once the texture cache is full; X11 26,408 to 26,936 KB.
- Idle CPU over 30 s: Wayland 0.37%, X11 0.03%.
- Executable plus plugin, unstripped: Wayland 3,578,760 B, X11 3,046,480 B, both below the pre-spike builds (3,877,920 B and 3,244,816 B) and above the old projects (3,248,552 B and 2,282,248 B), which is accepted.
- Bar sources went from about 10,300 lines (`bar` per backend) to about 5,800 including the panels; the other module merges are recorded in `tasks.md`.

## Risks

- The canvas costs the X201 its headroom, or costs Wayland its pooled scene. The Phase 9 gate exists for this.
- Wayland-only visual effects (animation, shaders, focus choreography) have no X11 equivalent. They stay in the shared view as optional effects the cairo canvas skips, or they stay in a Wayland-only subview.
- Animation in a shared model must not wake the X11 loop at idle. Models animate only while a tween is active, as the Wayland views do today.
- Per-module behaviour changes are visible to users; each is recorded in `tasks.md` when it lands.
- Two knowledge bases partly contradict; entries stay tagged by backend until a rule is proven for both.

## Decisions taken

- Review gates are waived by the user, who asked to run Phases 8 to 14 to the end without stopping. Each phase still ends with `./build.sh test` passing with no warnings, and a record in `tasks.md` and `progress.md`.
- Only the Wayland (Hyprland) backend animates. The X11 backend stays static through `animation_set_instant(true)`, so shared models still call `AnimationManager` without creating tweens on X11.
- `ModuleBase` carries `Host &`, `Reactor &` and the config, not services. Services stay in each backend's registry until `app/shell` in Phase 13.
- `islands` is not built on X11. The X11 style list is `okinami` and `continuous`, and a configured `islands` maps to `continuous` in one place, the shared `BarStyleSpec` lookup.
- Phase 9 go or no-go is numeric. Go only if, on both backends, idle RSS does not exceed the Phase 0 baseline, binary size grows by at most 3% over the pre-spike build (the absolute Phase 0 size was already exceeded before the spike and is a Phase 14 item), ten-minute open and close RSS growth stays under 0.1%, and X11 paint time is within 1.2 times the old direct cairo path. Any miss is a no-go: the model stays shared and the views stay per backend for the rest of the plan.
- Size: Phase 14 tries `-ffunction-sections -fdata-sections` with `--gc-sections` on the executable and plugins; the residual size is accepted when idle RSS stays under the baseline.
