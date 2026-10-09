# `astralia-shell` handoff

## Status

The merge of `astralia-shell-hl` (Wayland, Hyprland) and `astralia-shell-i3` (X11, i3) into one `astralia` executable is done: Phases 0 to 14 and the `hl` panel restyle. Tests pass with no warnings through `./build.sh test` (`unit`, `wayland-unit`, `x11-unit`, `backend-smoke`). The old repos are untouched and remain the rollback. The user has not reviewed any phase.

## Architecture in brief

- `astralia` detects the session (`ASTRALIA_BACKEND` override, `HYPRLAND_INSTANCE_SIGNATURE`, `SWAYSOCK`, `I3SOCK`, `XDG_SESSION_TYPE`) and `dlopen`s `libastralia-wayland.so` or `libastralia-x11.so` from `/usr/lib/astralia/`.
- A module is a display-free model, one view over `ui::Canvas` and a thin host per backend. `GlCanvas` (GLES2 with a scene graph) draws on Wayland and `CairoCanvas` (`cairo-xcb` with pango) on X11.
- Config is one JSON file, `~/.config/astralia/config.json`, for both backends. The legacy `~/.config/astralia-shell/{settings,wallpaper}.conf` are imported only when no JSON exists, so that directory is safe to delete once `config.json` holds the values.
- Rules live in `convention.md` and `knowledge.md`; the file map is `index.md`.

## Size and budgets

- Source is about 42,100 lines against about 52,000 for the two old projects: about 21,000 shared, 16,700 Wayland-specific, 4,400 X11-specific.
- Idle RSS: Wayland 112 MB against `hl`'s 182 MB, X11 26 MB against `i3`'s 29.5 MB. Idle CPU: Wayland 0.37%, X11 0.03%.
- Executable plus plugin, unstripped: Wayland 3.58 MB against 3.25 MB, X11 3.05 MB against 2.28 MB. The excess is accepted.

## Open items

- User review of every phase.
- The X201 hardware check, a real Sway session, and the old projects' idle CPU baseline.
- The Wayland tray popup (`xdg_popup`) is untested live because it needs a right-click and input injection is forbidden.
- Wayland panel clicks, hover, drags, the password dialog, `notification` on-screen and the `polkit` prompt are covered only by tests and the X11 run.
- `audio` and `media` came from `i3`; `hl`'s volume-slider behaviour is unchecked.
- The Wayland PAM check uses `UserService::name()` (the `gecos` name) as the PAM user, as `hl` did; use `pw_name` if the lock rejects a correct password.
- `core/config_file` still carries the `key = value` parser for the legacy import and the Wayland config adapter; remove it with the import.
- `Config` carries Wayland-only fields (`visualizer`, `rain`, idle) that are inert on X11, and `rain_config.h` repeats its constants in `src/config/` and `src/backend/wayland/config/`.
- The Wayland logout lock button runs `astralia lock`; the old install still has `/usr/bin/astralia-shell`. `./build.sh install` has not been run for the merged build.
- `watch_client_order` matters only on Hyprland; the dock and overview views must set it when shown and clear it when hidden.
- The launcher directory listing runs `fd` through `popen` on the reactor thread and blocks briefly.

## Design position on rendering

- Two canvases are deliberate. The shared canvas cost nothing measurable (osd paint 143.0 us against 142.3 us), and Wayland's idle numbers depend on the pooled scene graph.
- Cairo everywhere would remove the GL renderer, shaders and `GlCanvas` and cut the most code, but would lose the GPU effects and the cheap animated and video surfaces. It is unmeasured.
- GLES2 on X11 should run on the X201 (Ironlake, Mesa `crocus`, ES 2.0), but would add Mesa and a GL context to a 26 MB process and stress a weak GPU. It is unmeasured; test with a Phase 9 style spike before committing.
- A much smaller code base needs a product decision on the Wayland-only features, not a refactor.

## Resuming

- Install dependencies with `./build.sh setup all` (Arch `pacman`), then build and test with `./build.sh test`.
- Wayland live test: stop the running `astralia`, run `ASTRALIA_BACKEND_DIR=$PWD/build build/astralia debug` from the project root, toggle overlays with `build/astralia <verb>`, `build/astralia kill`, then restart the installed `astralia` with `setsid`. Kill by the PID from `$!`, never `pkill -f`.
- Wayland code runs only in a real Hyprland session. Do not view full-screen screenshots, do not inject keystrokes, and never live-test lock-screen or input-grabbing code carelessly.
- X11 live test: `Xephyr :9 -screen 1280x800` (no `-resize`), a nested `i3 -c /dev/null` on `DISPLAY=:9`, `unset I3SOCK`, then run the binary there.
- Smoke programs: build a `PollReactor` and a `SystemBus`, construct the service, pump `reactor.run_once(50)` for about two seconds and print the state. Compile with `g++ -std=c++23 -Isrc smoke.cpp build/libastralia-core-shared.a build/libastralia-core-base.a $(pkg-config --cflags --libs sdbus-c++ libpipewire-0.3 polkit-agent-1)`.
- Git: only `git diff` is allowed. The longer history is in the gitignored `local/wrapup.md`.
