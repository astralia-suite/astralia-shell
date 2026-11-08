# Wayland surfaces and protocol

Layer-shell, `xdg` and session-lock surfaces, input routing and teardown order. Sources: `src/backend/wayland/render/layer_surface.cpp`, `src/backend/wayland/render/popup_window.cpp`, `src/backend/wayland/render/toplevel_window.cpp`, `src/backend/wayland/app/monitor_output.cpp`, `src/service/wayland/input_service.cpp`, `src/modules/lock/wayland/`.

## Input

- [wayland] Wayland cannot tell which output the pointer is over. Track a best-effort hint from your own surfaces' enter and motion events.
- [wayland] Clear the pointer hint when its output is removed. A stale pointer to a freed output caused a use-after-free.
- [wayland] A `PerMonitorModule` pointer move fires on every monitor with the same coordinates. Guard hover on `pointer.focused_surface` being its own.
- [wayland] The pointing-hand cursor needs `wants_hand_cursor()` on both `Module` and `PerMonitorModule`. Otherwise bars keep the arrow.
- [wayland] Every panel with exclusive keyboard interactivity needs its own key-dispatch arm. Nothing enforces the two stay in sync.
- [wayland] A modifier-aware shortcut matches `KeyEvent::base_sym`, not `text`. `Shift`+`1` is `!` in `text`.
- [wayland] A hover highlight clears on lost surface focus, not only on motion. Stolen focus leaves a stale hovered index.

## Protocol

- [wayland] Optional protocol events need sane fallbacks, not zero. Some compositors never send `repeat_info`, and `0/0` disables key repeat.
- [wayland] A bind's reply is a second round-trip. Do one extra roundtrip before reading output state.
- [wayland] A live-update IPC event may lack a field only a full snapshot has. Check whether an adjacent event carries it and can cache it.
- [wayland] A struct member cannot share a name with a protocol type in the same header. `xdg_surface *xdg_surface` breaks lookup downstream.

## Layer shell

- [wayland] A coordinate from one layer-shell surface is invalid on another without translating margins. Origins differ.
- [wayland] Hover-driven surface changes touch size only, never margin or exclusive zone. A margin change repositioned mid-hover and looped.
- [wayland] `exclusive_zone` excludes the same-edge margin. The compositor adds it already, so including it reserves the space twice.
- [wayland] Change a bottom-anchored layer surface's size and buffer in one commit. A separate commit dropped hover.
- [wayland] Keep layer overlays mapped across same-output toggles. Recreating a same-namespace surface left it uncomposited or unrouted on Hyprland.
- [wayland] A mostly click-through layer surface takes clicks through a `wl_region` union of sub-rects. Rebuild the union whenever the rects change.
- [wayland] A full-output overlay that paints its own opaque backdrop must paint one empty frame after release. A closed surface keeps its last buffer.

## Toplevels and popups

- [wayland] Create a real `xdg_toplevel` on open and destroy it on close. A mapped transparent toplevel still shows in switchers.
- [wayland] `ToplevelWindowBase` has no resize callback. A module with a per-size buffer compares live width and height each paint.
- [wayland] A destroy-on-close toplevel can reuse an earlier window's address. State keyed by address must clear when the window goes.
- [wayland] An `xdg_popup` needs the parent layer surface, an anchor rectangle and the triggering input serial. Without the serial the compositor denies the grab.
- [wayland] A popup's `done` event dismisses it. Destroy the popup and notify the panel, since the compositor has already dropped the grab.

## Teardown

- [wayland] Reset a torn-down window's `FrameClock` fields as well as its handles. A stale non-null callback silences `request_frame` forever.
- [wayland] Release a pending `wl_callback` with `wl_callback_destroy`, never by nulling. `wl_surface_destroy` does not free it.
- [wayland] Release an `EGLSurface` from the current context before `wl_egl_window_destroy`. A current surface defers teardown into freed memory.
- [wayland] Call `gl_release_if_current` before every `EGLSurface` teardown. It unbinds only that surface, keeping the context bound surfacelessly.
- [wayland] A per-monitor `apply_config` reads its `new_cfg`, never `app.cfg`. The latter is assigned after the fan-out.
- [wayland] A per-monitor `create_surface()` must not gate on config state. It runs once, so a startup check broke toggle-on.
- [wayland] Every per-monitor `destroy()` drops its pending frame callback. A late `frame_done` otherwise runs on freed state.
- [wayland] A per-monitor layer surface's `.closed` handler is a no-op. `registry_global_remove` owns cleanup; exiting there kills the whole shell.
- [wayland] `registry_global_remove` tells overlays an output is gone via `Module::on_output_removed`. Otherwise they keep a dangling `bound_output`.
- [wayland] A per-monitor text-input client clears its own focus in `destroy()`. `PerMonitorModule::destroy()` bypasses `on_output_removed`.

## Session lock and auth

- [wayland] Start the polkit agent only after keyboard input and a GLib loop exist. An agent that cannot prompt fails every `pkexec`.
- [wayland] `ext-session-lock` withholds `locked` until every output presents a non-null buffer. Paint all lock surfaces before flushing.
- [wayland] Grab a transition snapshot with `glCopyTexImage2D` after `flush` and before the swap. The texture then outlives the frame.
- [wayland] Set keyboard interactivity to none and commit on overlays that outlive the lock. Otherwise they keep exclusive focus.
- [wayland] Never release the session lock inside a transition `configure` handler. Defer it with `DeferredCall`.
- [wayland] Poll a deadline from the module timer to finish a stuck transition. The lock request then still runs.
- [wayland] `unlock_and_destroy` needs a `wl_display_roundtrip` before teardown. Otherwise the server may kill the client with a protocol error.
- [wayland] Ack a lock-surface `configure` before any commit and on every resend. Defer the `wl_egl_window` resize past the handler.
- [wayland] Do not gate the startup overlay's `init_egl` loop on `surface()`. The lock owns none until locked.
- [wayland] A `pam_start_confdir` service needs an `account` rule. Without it `pam_acct_mgmt` returns `PAM_PERM_DENIED` on a correct password.
