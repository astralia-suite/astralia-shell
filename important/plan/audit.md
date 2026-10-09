# Phase 0 audit and baseline

## Baselines

Measured on this machine; both projects built in a scratch directory with `optimization=2`, `debug=false`, `-j4`, so nothing in the old repos changed.

| | `hl` | `i3` |
|---|---|---|
| Executable size | 3,248,552 B (`.text` 2,558,578 B) | 2,282,248 B (`.text` 1,846,264 B) |
| Extra files | `libastralia-shell-media.so` 75,616 B | none |
| Build time (full, `-j4`) | 3 min 57 s (901 s user) | not captured |
| Idle RSS | 182,364 KB 7 s after start, 186,576 KB after opening `launcher`, `logout` and `dashboard` and waiting 10 s, about 7% CPU at that point (optimized build, live Hyprland session on `eDP-1`, wallpaper loaded) | 29,552 KB (installed binary, running now) |
| Installed binary in `/usr/bin` | 53,365,160 B, a `debug` / `-O0` build | 2,200,328 B |

- The installed `/usr/bin/astralia` is a debug build, so it is not a valid size or speed baseline; the optimized numbers above are.
- The `hl` idle RSS must be measured from a Hyprland session before Phase 5 can claim no regression.

## Parser comparison

Benchmark in the scratch directory, `-O2`, 20,000 parses each. The `clients` sample is synthetic (8 clients with Hyprland's usual fields), because no Hyprland session was available to capture a real reply.

| Input | `nlohmann_json` | in-house `json.cpp` |
|---|---|---|
| `config.json` (1,299 B) | 54.4 µs | 16.3 µs |
| synthetic `j/clients` (4,209 B) | 201.4 µs | 51.6 µs |

- The in-house parser is about 3.3x to 3.9x faster.
- In absolute terms the gain is small: the config parses once per change, and the `clients` re-read costs about 0.02% of one core at once per second. The real saving is dropping a template-heavy dependency, with its compile time and `.text` size.
- Decision: JSON stays the file format, parsed by the in-house parser, with a writer added in Phase 1. The `nlohmann_json` dependency is removed from both backends.

## Core files

| File | `hl` | `i3` | Action |
|---|---|---|---|
| `log` | 107 lines, `klog`, crash handler, file logging | 12 lines | Keep the `hl` one; its crash and `std::terminate` handler is needed |
| `async_process` | struct plus `async_process_poll`, polled by the loop | class with `EventLoop` and a `Done` callback | Keep the `i3` interface on top of `Reactor`; keep the `hl` `posix_spawn` and generation counter |
| `poll_source`, `deferred_call`, `path_home` | present | absent | Move to shared core |
| `cli`, `config_file`, `daemon`, `dbus`, `ipc`, `json`, `runtime_paths`, `signal`, `single_instance`, `spawn`, `unique_fd`, `allocator`, `keyboard` | absent | present | Move to shared core; `keyboard` and `x_connection` stay in the X11 backend |
| IPC socket path | `$XDG_RUNTIME_DIR/astralia-shell.sock` | per display via `runtime_paths` | Use the `i3` per-session scheme |

## Services

| Pair | `hl` lines | `i3` lines | Note |
|---|---|---|---|
| `bluetooth` | 541 | 234 | Reconcile |
| `network` | 656 | 482 | Reconcile |
| `brightness` | 89 | 79 | Both use `brightnessctl` |
| `tray` | 258 | 250 | Near equal |
| `settings` | 69 | 191 | `i3` holds more of the logic |
| `wallpaper` | 49 | 108 | `i3` holds more of the logic |
| `polkit` | 742 | 573 | `hl` has the GLib poll source and the agent-startup rule |
| `notification` | 112 | 119 | Near equal |

- Differently named equivalents: `pipewire` (`hl`) vs `audio` (`i3`), `upower` (`hl`) vs `battery` (`i3`), `mpris` plus `media` (`hl`) vs `media` (`i3`). Their contents are not compared yet; Phase 2 reads them first.
- `hl` only, display-free: `dock`, `icon`, `telemetry`, `mpris`. `hl` only, Wayland: `capture`, `frame`, `idle`, `input`, `output`, `text_input`. `hl` compositor: `compositor`, `hyprland`.
- `i3` only: `i3`, `user`. `output` touches X in `i3`.
- Services in `i3` already take `EventLoop &` and publish through `Signal`, which is the shape the shared `Reactor` interface follows.

## Modules

Rendering is where the two diverge most, so the shared part of a module is smaller than the shared name suggests.

| Module | `hl` lines | `i3` lines | Shared model |
|---|---|---|---|
| `launcher` | 936 | 581 | Large. The `launcher/` provider files are the same set in both; `i3` adds `search_process` |
| `bar` (styles, widgets, panels) | 883 | 507 | Medium. `styles/geometry`, `islands`, `okinami`, `continuous` exist in both |
| `settings` | 651 | 259 | Medium. Tabs are `hl` GL draws vs `i3` cairo; tab model and `layout` are shareable |
| `overview` | 801 | 387 | Medium |
| `logout` | 839 | 213 | Small. `i3` shares only button geometry; `hl` is mostly animation and GL shaders |
| `notification` | 478 | 138 | Small |
| `polkit` | 312 | 247 | Small |
| `osd` | 271 | 134 | Small |
| `wallpaper` | 663 | 123 | Small, the service holds the logic |
| `lock`, `idle`, `dashboard`, `rain`, `visualizer` | 618, 264, 108, 193, 284 | none | Wayland only, and `idle` stays so (i3 has its own) |

- Consequence for Phase 6: the real sharing is `launcher`, `bar` styles and models, `settings` and `overview` models, and the `layout` files. For `logout`, `notification`, `polkit`, `osd` and `wallpaper` the views stay separate and little moves.
- Both projects use the same module pattern for IPC verbs (`hl`: `ipc_handlers()` on the module; `i3`: `ipc` registration from the module's constructor). `hl` verbs include `kill`, `launcher`, `dashboard`, `rain` and `visualizer`; `i3` has `launcher` and `logout` among its toggles. Full verb tables are compared in Phase 1 when `ipc` merges.

## Tests

- Neither project uses a framework. `hl`: `test/test.cpp` declares about 40 `test_*()` functions defined across `test/**/*.cpp`. `i3`: one 851-line `test/main.cpp` with check helpers.
- Plan: keep the `hl` per-file layout and the `i3` check helpers in one shared header.

## Findings that change the plan

- The `hl` installed binary is a debug build; the baseline uses the optimized one.
- The `nlohmann` removal is justified by dependency weight, not by speed.
- Phase 6 is smaller than first estimated for five modules; the plan keeps them duplicated.

## Still unknown

- `hl` idle RSS (needs a Hyprland session).
- `i3` build time.
- Contents of `i3` `media_service` against `hl` `mpris` and `media` services.
- Whether each `hl` module's `ipc_handlers` verb names collide with `i3`'s.
