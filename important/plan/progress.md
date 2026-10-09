# Merge progress

## Purpose

This file lets another machine or session resume the merge of `astralia-shell-hl` (Wayland, Hyprland) and `astralia-shell-i3` (X11, i3) into this repo, `astralia-shell`, which builds one executable `astralia`. Read it first, then `merge-plan.md`, `audit.md` and `tasks.md` in this directory. Update the status below whenever a phase moves.

## Status

- Phase 0, audit and baseline: done. See `audit.md`.
- Phase 1, shared core: done. `src/core/` builds and its tests pass.
- Phase 2, shared services: done. Reviewed implicitly by moving on to Phase 3.
- Phase 3, compositor interface: done. Awaiting the user's review.
- Phase 4, backend interface and loader: done. Awaiting the user's review. The backends are stubs that only serve the IPC socket.
- Phase 5, backend move: done. Both backends run on the shared services; the Wayland backend is verified live in Hyprland, the X11 backend nested in `Xephyr`. Awaiting the user's review.
- Phase 6, shared module models: done. Awaiting the user's review. The launcher logic, overview paging, polkit and logout layouts, and the osd, overview, polkit and logout constants plus the icon glyph table are shared; `notification`, `settings`, bar styles and `wallpaper` stay per backend by design.
- Phase 7, docs and build options: done. `knowledge.md` merged and tagged by backend, `build.sh` backend options, `readme.md`.
- Single module directory: done. Every module's backend views sit in `src/modules/<name>/{wayland,x11}/`, backend-only services in `src/service/{wayland,x11}/`. Layout only, no behaviour change.
- Phase 8, shared contracts: done. `core/animation`, `core/input.h`, `ui/tokens.h`, `ui/glyphs.h`, `ui/host.h`, `ui/module_base`.
- Phase 9, canvas and spike: done, go. `osd` and `polkit` are one model and one view over `ui::Canvas`; `GlCanvas` on Wayland, `CairoCanvas` on X11. Results in `merge-plan.md`.
- Phase 12, bar: done. One `BarModel`, one `paint_bar`, one `PanelSet` with nine panel contents, `PanelSurface` on Wayland and `PanelHost` on X11; only the frame painters stay per backend.
- Phase 13, glue and services: done. `app/shell` owns the verb table, capabilities and the open-module list; the alias headers are gone; `SWAYSOCK` selects Wayland with `I3Compositor`.
- Phase 14, capabilities and finish: done. Capabilities gate the Wayland-only modules, the core is split so plugins keep only what they use, budgets and docs are recorded in `tasks.md`.

## Decisions already taken

- One executable `astralia`. Each session loads one backend library with `dlopen` from `/usr/lib/astralia/`: `libastralia-wayland.so` or `libastralia-x11.so`.
- Session detection order: `HYPRLAND_INSTANCE_SIGNATURE`, then `I3SOCK`, then `XDG_SESSION_TYPE`. `ASTRALIA_BACKEND=wayland|x11` overrides.
- Rendering stays per backend: the Wayland backend keeps its GLES2 renderer, the X11 backend keeps `cairo-xcb`. The full merge puts one `Canvas` interface over both so each module has one view; Phase 9 decides go or no-go against the resource budgets, and on no-go the views stay per backend with a shared model.
- The X11 backend is still-image only, so the `ffmpeg` media plugin is not ported. `idle` is not ported to X11. `lock` and `dashboard` stay Wayland-only.
- Config is one JSON file, `~/.config/astralia/config.json`, parsed by the in-house `core/json`. `nlohmann_json` is dropped everywhere. The old `i3` files `~/.config/astralia-shell/settings.conf` and `wallpaper.conf` are imported once when no JSON config exists.
- Shared services follow the `i3` shape: a class that takes `SystemBus &` or `Reactor &` and publishes through `Signal`. They follow the `hl` rule that no D-Bus read blocks the reactor thread.
- IPC socket, lock and log names use `astralia-<session>` where the session is `WAYLAND_DISPLAY`, else `DISPLAY`. The log file is `~/.local/state/astralia/astralia.log`.
- The old repos are never modified except their `build.sh`, which was replaced with the shared script. They remain the rollback.
- A module is `src/modules/<name>/`: display-independent files at the top, each backend's view and module-only config in `<name>/wayland/` and `<name>/x11/`. A backend config used by several modules or by `render/` stays in the backend's `config/`.
- Review gates are waived and the open questions of `merge-plan.md` are settled (see its "Decisions taken"): only Wayland animates, `ModuleBase` carries no services, `islands` is not built on X11, the Phase 9 go or no-go is numeric. Phases 8 to 14 run to the end without stopping.
- `merge-plan.md` was rewritten on the user's request to describe the full merge; the first seven phases are summarised there and recorded in `tasks.md`.

## Repository layout on the author's machine

Everything lives under `~/astralia-suite/`. The merge target is `astralia-shell/`. The sources being merged are the sibling directories `astralia-shell-hl/` and `astralia-shell-i3/`. Another machine needs both of them next to this repo to port code from. The user forbids `git push`, `pull`, `commit` and branching; use copy and move, and `git diff` only.

## What exists

- `src/core/`: reactor and `PollReactor`, log, JSON with writer, config file helpers, IPC, CLI, runtime paths, single instance, daemon, async process, spawn, D-Bus helpers, deferred call, path home, allocator, signal, unique fd.
- `src/service/`: `audio`, `battery`, `bluetooth`, `brightness`, `compositor` (with `hyprland` and `i3` implementations), `dock`, `icon`, `media`, `network`, `notification`, `polkit`, `settings`, `telemetry`, `tray`, `user`, `wallpaper`.
- `src/config/`: `bar_style.h`, `visualizer_config.h`, `rain_config.h`, `icons.h` and the shared `launcher`, `polkit`, `logout`, `osd` and `overview` configs. Each module's backend view constants are `*_style_config.h` beside its view.
- `src/modules/<name>/`: one directory per shell part, shared files at the top and each backend's views in `<name>/wayland/` and `<name>/x11/`. Backend-only services are in `src/service/wayland/` and `src/service/x11/`.
- `test/`: plain check functions, `test/main.cpp` is the runner. Run with `./build.sh test`.
- `src/app/`: `Backend` interface and session detection plus the `dlopen` loader. `src/backend/{wayland,x11}/`: the two backend plugins. `src/main.cpp`: the real entry point.
- `build.sh`: `setup [wayland|x11|all]`, `build`, `test`, `install`, `run`, `uninstall`.
- `important/`: `convention.md`, `knowledge.md`, `index.md` and this plan directory.

## How to resume on a new machine

1. Install dependencies: `./build.sh setup all` (Arch `pacman`). The shared library needs `sdbus-c++`, `libpipewire-0.3`, `polkit-agent-1` and `meson`.
2. Build and test: `./build.sh test`. Expect four passing tests: `unit`, `wayland-unit`, `x11-unit` and `backend-smoke`, and no warnings.
3. Read `important/convention.md` before writing code: no comments, root-relative includes from `src/`, `clang-format -i` on every touched file, one `**_service.{h,cpp}` pair per service.
4. Read "What is left" below.

## What is left

Phases 0 to 14 are done. What remains is the user's review and the open points below.

- Review gates: every phase is checked by tests and live runs but none has been reviewed by the user.
- Size: executable plus plugin is 3.58 MB for Wayland against `hl`'s 3.25 MB and 3.05 MB for X11 against `i3`'s 2.28 MB, unstripped. Idle RSS is far lower: 26 MB for X11 against 29.5 MB and 112 MB for Wayland against 182 MB. The split core, hidden visibility and `--gc-sections` brought both below the pre-spike sizes; the rest is accepted.
- The Wayland logout lock button runs `astralia lock`. The old install still has `/usr/bin/astralia-shell`; the new one does not.
- Not done: the X201 hardware check, a real Sway session, and the old projects' idle CPU baseline (the merged build idles at 0.37% on Wayland and 0.03% on X11).

Procedure for live tests: build, run `./build.sh test`, stop the running `astralia`, run `ASTRALIA_BACKEND_DIR=$PWD/build build/astralia debug` from the project root, toggle overlays with `build/astralia <verb>`, `build/astralia kill`, restart the installed `astralia` with `setsid`. Kill by the PID from `$!`, never `pkill -f`. Do not view full-screen screenshots of the real session; they capture private windows. Do not inject keystrokes into the real session.

## Known gaps and unverified points

- Wayland live checks cannot click or type. The bar panels were opened through the `panel-*` verbs and screenshots; hover, drags and the password dialog are covered by tests and the X11 run only. `notification` (D-Bus name served, `notify-send` accepted, no on-screen check), `polkit` (agent registers; no authentication prompt triggered), network and bluetooth panels (no clicks). Their pure logic is tested. Smoke runs for `battery`, `bluetooth`, `network`, `telemetry` and config loading were done against the real system.
- `audio` and `media` were taken from `i3` after reading only the headers and `hl`'s pipewire route handling. `hl`'s volume-slider behaviour is still unchecked.
- The Wayland PAM check and lock card use `UserService::name()`, the display name from `gecos`, as the PAM user, as `hl` did. If the lock rejects the right password on a machine whose `gecos` differs from its login, use `pw_name` there.
- The launcher search no longer waits for a killed search to die before restarting; the shared `AsyncProcess` ignores a cancelled run's result by generation.
- `config_file.{h,cpp}` still carries the `key = value` parser, used only for the legacy import. Remove it once the import is no longer needed.
- `BarStyle` is `islands`, `okinami`, `continuous`. `bar_style_resolve` maps `islands` to `continuous` on X11, in `modules/bar/style.cpp` only.
- `Config` still carries Wayland-only fields (`visualizer`, `rain`, idle). They are inert on X11.
- `knowledge.md` keeps the `hl` entries under `[wayland]` and the `i3` entries under `[x11]`; a few may describe code that moved to the shared layer.
- The compositor `watch_client_order` flag is only meaningful on Hyprland; the dock and overview views must call it when they are shown and clear it when hidden.
- The i3 compositor reports one `Workspace` list per output and numbers workspaces globally, so it ignores the `global` paging flag. Workspace names without a number are dropped. The X11 bar shows ten pills through `workspace_slots`.
- The optimized `hl` build used about 182 MB idle and `i3` about 29 MB; the merged build is below both. See `audit.md` and `tasks.md`.

## Testing notes for the next phases

- A module's headless tests go in `test/modules/<name>/`; view-level checks in `test/modules/<name>/<backend>/`. The Wayland launcher search has a headless test that drives the real search path with a `PollReactor` and no display.

- Wayland code can only be run inside a real Hyprland session. Hyprland cannot run nested under X11 or headless here. To test `hl` code, build into a scratch directory with `--prefix=/usr` so it finds the installed assets, stop the running `astralia` first and restart it afterwards.
- X11 code can be run nested: start `Xephyr :9 -screen 1280x800` (no `-resize`), run a nested `i3 -c /dev/null` on `DISPLAY=:9`, then run the binary there. `unset I3SOCK` first. Paths are per display, so the user's shell on `:0` is not affected.
- Never live-test lock-screen or input-grabbing code carelessly; a past test hung the keyboard and forced a reboot.
- Smoke programs are tiny: create a `PollReactor` and a `SystemBus`, construct the service, pump `reactor.run_once(50)` for about two seconds, print the state. Compile with `g++ -std=c++23 -Isrc smoke.cpp build/libastralia-core-shared.a build/libastralia-core-base.a $(pkg-config --cflags --libs sdbus-c++ libpipewire-0.3 polkit-agent-1)`.
- `grim` screenshots capture the whole screen and can include private windows. Delete them right after viewing and do not keep them.
