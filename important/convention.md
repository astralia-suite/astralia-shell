# `astralia-shell` developing conventions

## Commenting

- Applies to C++ sources (`src/**`, `test/**`); scripts such as `build.sh` may keep a usage header.
- No comments across the code base.
- Exceptions:
  - Namespace comment.
  - Comments that group constants together, wherever those constants live (`src/config/` or a file's own constants).
  - License/attribution notices for third-party code.

## Formatting code

- Command: `clang-format -i <filename>`, for every `.h` and `.cpp` touched.
- Style taken from `.clang-format` at the project root.

## Source layout

- `src/main.cpp`: detects the session, loads the matching backend plugin, runs it; nothing else.
- `src/app/`: process-wide shared state and the backend interface; never includes `backend/`.
- `src/core/`: non-visual infrastructure (logging, processes, IPC, JSON, config file, D-Bus); includes nothing from `render/`, `modules/`, `service/` or `backend/`.
- `src/config/`: per-module constants and plain data types.
- `src/service/`: data providers; those both backends use sit at the top, backend-only ones in `wayland/` and `x11/`; the compositor services sit behind the `Compositor` interface in `compositor_service.h`.
- `src/modules/`: one directory per shell part, with its shared files at the top and each backend's view in `<name>/wayland/` or `<name>/x11/`.
- `src/backend/wayland/`: Wayland, EGL and GLES2 infrastructure (`app/`, `core/`, `render/`, `protocols/`), built into `libastralia-wayland.so` together with the `wayland/` module and service files.
- `src/backend/x11/`: `xcb` and `cairo-xcb` infrastructure (`app/`, `core/`, `render/`), built into `libastralia-x11.so` together with the `x11/` module and service files.
- `src/plugin/`: optional `dlopen`-loaded code owned by a service.
- `test/`: tests for `src/` code, built as `astralia-test`; module tests sit in `test/modules/<name>/` and `test/modules/<name>/<backend>/`.

## Backend boundary

- The `astralia` executable links `astralia-core-base` (`src/app/`, `src/core/`) whole and exports its symbols; plugins resolve those at run time and link `astralia-core-shared` (services, `ui/`, modules) statically, keeping only what they use.
- Plugin code is built with hidden visibility and `--gc-sections`; the only exported plugin symbol is `astralia_backend_create`.
- The `astralia` executable links no display library; `xcb`, `wayland`, `EGL`, `GLES` and `ffmpeg` are linked only by the backend that needs them.
- A backend is selected at startup from the environment and loaded with `dlopen`; the core never names a backend type.
- Code shared by both backends lives outside `src/backend/`; a backend never includes the other backend.
- Each backend sets its own compile flags and `malloc` tuning.

## Module boundary

- A module is `src/modules/<name>/`: display-independent files at the top, and each backend's view and module-only config under `<name>/wayland/` and `<name>/x11/`. A backend's meson list compiles only its own subdirectory.
- A module's `wayland/` and `x11/` files never include each other.
- A module is not allowed to include files from another module, its private components included, except `bar` panels, which use `service/` and `ui/` only.
- A module is a model (state, logic and animation targets, no display include), one view over `ui::Canvas`, and a thin host per backend.
- A view never includes a backend header; backend-only drawing goes through a hook the host supplies (`LogoutLogoPainter`, `OverviewTileArt`, `SettingsArt`).
- Only Wayland animates: models call `AnimationManager` freely and X11 sets `animation_set_instant(true)`, so nothing in a model checks the backend.
- A feature one backend lacks is a `Capabilities` flag in `app/shell.h`; hosts and verbs register only for flags the backend reports.
- A module shall manage its internal works, without bleeding into `main.cpp`.
- `main.cpp` shall not include specific components belonging to a module.

## Config headers

- `src/config/*.h` holds constants and plain data types only, no function bodies (helpers that compute from a config value live with their consumer).
- One config header per module, named `<module>_config.h`; a module's private components share it. The bar has two: `bar_layout.h` for the bar and `panel_config.h` for its panels.
- Constants a shared view draws with live in the module's shared config header; constants only one backend's host uses live in `src/modules/<name>/<backend>/<module>_style_config.h`. A backend config used by several modules or by `render/` stays in the backend's `config/`.

## Service structure

- `src/service/` holds as many services as needed, but limited to one pair of `**_service.{h,cpp}` per service.
- `src/plugin/` holds `dlopen`-loaded `shared_module`s, one `**_plugin.{h,cpp}` pair each, loaded by their owning service.

## Build targets

- `src/app/` and `src/core/` compile into `astralia-core-base`, everything else shared into `astralia-core-shared`; `astralia-test` links both.
- `meson.build` adds `include_directories('src')` to every target.
- Tests use no framework: `test/main.cpp` runs plain check functions and returns non-zero on failure; `meson test -C build` runs it.
- Tests cover pure logic only; nothing in `test/` opens a display connection.
- Test-local helpers such as `check.h` and `core/pump.h` live under `test/` and are included without the `src/` root.

## Includes

- Headers use `.h`, sources use `.cpp`.
- Every local `#include` is root-relative from `src/`, e.g. `#include "core/log.h"`, never `../` or a bare filename.
- A backend has its own include root. x11 uses `src/backend/x11/` (`"render/draw.h"`); wayland uses `src/backend/` and prefixes its headers (`"wayland/render/renderer.h"`) because its service headers share names with the shared ones. Module and service files, wherever they sit, are relative to `src/` (`"modules/bar/wayland/bar.h"`), and file names must not collide between roots.
- `test/**` includes `src/` headers the same root-relative way, e.g. `#include "core/log.h"`.
- Generated Wayland protocol headers stay bare filenames since they build outside `src/`.
- Header order:
  - system headers (`<header>`), one block; a blank line may split it only where include order matters and `clang-format` would otherwise reorder it
  - one blank line
  - local headers:
    - `"dir1/local_header.h"`
    - blank
    - `"dir2/local_header.h"`
