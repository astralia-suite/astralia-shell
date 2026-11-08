# Compositors

Behaviour of the window managers behind the `Compositor` interface. Sources: `src/service/compositor_service.cpp`, `src/service/i3_service.cpp`, `src/service/hyprland_service.cpp`, `src/service/shojiwm_service.cpp`, `src/service/theme_service.cpp`.

## i3

- i3 exports `I3SOCK`, not `I3_SOCK`. The socket path also lives in the `I3_SOCKET_PATH` property on the X root window.
- Subscribe to i3 `workspace` and `window` events on a dedicated socket. A refresh per event replaces watching root properties.
- An i3 workspace exists only while it holds windows or is visible. The list is not a fixed five, so pills need `workspace_slots`.
- Derive i3 tiled rects from `layout` and `percent`, not `GET_TREE` rects. i3 skips rendering hidden workspaces, leaving them stale.
- Wait for the i3 `RUN_COMMAND` reply before querying again. Otherwise the tree read can precede the command's effect.
- Ignore focus loss briefly after an i3 command, then refocus. i3 hands focus to a client on workspace switches.

## Hyprland

- Hyprland emits `createworkspacev2` and `activewindowv2` before `workspacev2`. Treat any structural event in a batch as a full refresh.
- Refresh on `activewindowv2` too. Focus changes bump every client's `focusHistoryID`, so UI ordered by it goes stale.
- Hyprland emits no event for a tiled reorder inside a workspace. Client re-reads are opt-in through `watch_client_order`.
- Give every Hyprland request on the poll thread a socket timeout, read as "no change". An empty reply parsed as no clients blanks the dock.
- Hyprland border colours go through the request socket as `eval hl.config(...)`. `configreloaded` resets them to the user's config, so `HyprlandCompositor` re-sends the last pair on that event.
- Hyprland's classic `dispatch` strings are deprecated for Lua. `HyprlandCompositor::dispatch` takes a `hl.dsp.*` expression.
- `j/getoption <name>` works over the existing request socket. No need to shell out to `hyprctl`.
- Pass `global=true` on every tile and focus call of a workspace grid spanning monitors. Hyprland otherwise remaps ids onto the focused monitor's page.

## ShojiWM

- ShojiWM workspace and window state comes from its TypeScript config, not the compositor core. The core socket `$SHOJIWM_SOCKET` only takes `quit` and `reload`; the config socket is `$XDG_RUNTIME_DIR/shojiwm-$WAYLAND_DISPLAY.sock`.
- ShojiWM workspaces are per monitor (`index` 1 to 10 on each output). `ShojiCompositor` maps them to `ordinal * 10 + index` so ids stay unique and the overview pages line up with monitors; labels keep the bare index.
- A ShojiWM config reload closes and recreates the IPC server. `ShojiCompositor` retries the event socket every two seconds and refreshes on reconnect.
- ShojiWM IPC handlers that return nothing reply `{"id":n}` with no `result`, since `JSON.stringify` drops `undefined`. Read a missing `result` as success.

## Shared

- Refresh compositor state before an overlay reads it on open. Event-driven state can be stale by then.
- Compositor state is not ready when the first monitor's surface is created. Sizing from it needs a later catch-up.
- `Config::palette_source` picks the theme. `default_palette` is the fixed Keqing extraction and ignores the wallpaper; `region_capture` extracts from the wallpaper that `capture_monitor` and `capture_column` choose. A fixed `ThemeSource` applies synchronously.
