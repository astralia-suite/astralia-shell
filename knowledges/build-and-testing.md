# Build and testing

How the core libraries, the `astralia` executable and the backend plugins link together, and how tests and live sessions stay isolated from the user's running shell. Sources: `meson.build`, `meson.options`, `build.sh`, `src/app/session.cpp`, `src/core/runtime_paths.cpp`, `test/`.

## Build and linking

- Link a core library into the executable with `link_whole` and `export_dynamic`. Plugins resolve those symbols at `dlopen`.
- Never `dlclose` a plugin. Its vtables and registered handlers outlive any point where closing would be safe.
- Link a backend's static library with `link_whole` and `pic: true`. The plugin and its unit-test executable then share it.
- Declare each `shared_module` in the root `meson.build`. A subdirectory puts the `.so` away from the loader's search path.
- Compile generated protocol `.c` files into one library only. Two copies define every interface twice at link.
- Keep pure logic apart from EGL, GL and `xcb` code. The test binary then links no display library.
- A generically named `constexpr` can collide with an identical name in an unrelated header. Modules that never include each other can still meet transitively.

## Testing

- Tests set `XDG_STATE_HOME` to a temporary directory first. `log` opens the real state file on first use and would fill it.
- Point tests at a scratch `XDG_CONFIG_HOME`. The real config stays untouched.
- Never start a second shell instance from a test inside the user's session. It would compete for the real lock and socket.
- Live-test lock and input-grabbing code only with a recovery path ready. A stuck grab hangs the keyboard and forces a reboot.

## Session and tooling

- A session-named path comes from `WAYLAND_DISPLAY`, else `DISPLAY`. Hyprland also exports `DISPLAY` for XWayland, so the order matters.
- Never `pkill -f` a pattern the running shell command also contains. Kill the PID captured from `$!` instead.
- zsh breaks `${var^}` and unquoted word splitting inside heredoc loops. Generate files from a script instead.
- `grim` captures the whole screen, private windows included. Delete each screenshot right after viewing it.
- Writing literal Unicode escapes through tool calls is unsafe. The JSON layer can swap them for glyph bytes; verify with `od -c`.
