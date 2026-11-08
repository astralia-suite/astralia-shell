# `astralia-shell` developing conventions

## Commenting

- Applies to C++ sources (`src/**`, `test/**`); scripts such as `build.sh` may keep a usage header.
- No comments across the code base.
- Exceptions:
  - Namespace comment.
  - Comments that group constants together, wherever those constants live.
  - License/attribution notices for third-party code.

## Formatting code

- Command: `clang-format -i <filename>`, for every `.h` and `.cpp` touched.
- Style taken from `.clang-format` at the project root.

## Boundary

- `src/main.cpp` loads the matching backend plugin and runs it; it includes no module component.
- `src/app/` never includes `backend/`; `src/core/` includes nothing from `render/`, `modules/`, `service/`, `app/` or `backend/`.
- The `astralia` executable links no display library; `xcb`, `wayland`, `EGL`, `GLES` and `ffmpeg` belong to the backend that needs them.
- A backend is selected at startup from the environment and loaded with `dlopen`; the core never names a backend type.
- Plugin code is built with hidden visibility and `--gc-sections`; the only exported plugin symbol is `astralia_backend_create`.
- Code shared by both backends lives outside `src/backend/`; a backend never includes the other backend.
- A module lives in `src/modules/<name>/`: display-independent files at the top, each backend's host under `<name>/wayland/` and `<name>/x11/`.
- A module's `wayland/` and `x11/` files never include each other.
- A module's code lives in `astralia::<module>`; a name drops the module word that the namespace already says (`launcher::toggle`, `lock::Model`).
- A module never includes files from another module, its private components included, except `bar` panels, which use `service/` and `render/` only.
- A shared view never includes a backend header; backend-only drawing goes through a hook the host supplies (`lock::Thunder`, `overview::TileArt`, `settings::Art`).
- A module manages its internal works without bleeding into `main.cpp`.

## Including

- Headers use `.h`, sources use `.cpp`.
- Every local `#include` is root-relative from `src/`, e.g. `#include "core/log.h"`, never `../` or a bare filename.
- A backend has its own include root. x11 uses `src/backend/x11/` (`"render/draw.h"`); wayland uses `src/backend/` and prefixes its headers (`"wayland/render/renderer.h"`) because its service headers share names with the shared ones. Module and service files, wherever they sit, are relative to `src/` (`"modules/bar/wayland/bar.h"`), and file names must not collide between roots.
- `test/**` includes `src/` headers the same root-relative way; test-local helpers such as `check.h` and `core/pump.h` live under `test/` and are included without the `src/` root.
- Generated Wayland protocol headers stay bare filenames since they build outside `src/`.
- Header order:
  - system headers (`<header>`), one block; a blank line may split it only where include order matters and `clang-format` would otherwise reorder it
  - one blank line
  - local headers:
    - `"dir1/local_header.h"`
    - blank
    - `"dir2/local_header.h"`
