# Wayland animation and frames

Frame-callback pacing and `AnimationManager` tweens. Sources: `src/service/wayland/frame_service.cpp`, `src/core/animation.cpp`, `src/backend/wayland/render/overlay_panel.cpp`.

## Frame callbacks

- [wayland] `request_frame` defers a mapped surface's repaint to `frame_done`. Inline `eglSwapBuffers` from a D-Bus handler froze the shell.
- [wayland] A deferred commit needs a one-pixel `wl_surface_damage_buffer`. Hyprland skips scheduling a frame for an undamaged commit.
- [wayland] `frame_done` arms the next frame callback before calling `draw()`. The paint's own commit must carry the next request.
- [wayland] A closed full-screen overlay must no-op its `request_frame`, gated on `open`. Blanket fan-outs otherwise repaint idle surfaces.
- [wayland] A closing overlay requests its frame before its fade completes. `on_complete` flips `open`, and a trailing call then never arms.
- [wayland] A host requests its frame before `model.close()`. With animations off the close ends inside the call, so a later request is dropped.
- [wayland] `redraw_all_monitors` pokes per-monitor modules only. An open overlay reacting to an event needs its own `request_frame` loop.
- [wayland] A paint that re-arms only while a tween is active needs every time-varying element tween-driven. A wall-clock bar froze otherwise.

## AnimationManager

- [wayland] `AnimationManager` advances only when `tick()` runs each frame. A panel that forgets freezes, `on_complete` callbacks included.
- [wayland] Repeated `tick()` calls per instant are safe. Progress comes from wall-clock time, not accumulated delta.
- [wayland] Two `animate()` calls sharing an owner id cancel each other. Give each animated property its own id.
- [wayland] An animation's `on_complete` that destroys its surface fires inside paint's `tick()`. Re-check validity after `tick()` or defer the destroy.
- [wayland] Defer teardown from an `on_complete` through `DeferredCall`. Freeing inline destroys the manager mid-iteration.
- [wayland] A global instant switch cannot reach a self-re-arming `on_complete` chain. It recurses forever, so such a chain checks it itself.
- [wayland] An animated per-frame layout value needs a snap-on-first-value sentinel. Otherwise the first frame tweens from a stale default.
- [wayland] A tweened value must not derive from another mid-tween value. It re-targets every frame instead of converging.
- [wayland] `AnimationManager` has no delay primitive. Chain a no-op tween whose `on_complete` starts the real one.

## Tweening

- [wayland] Drive a scripted entrance from one linear progress value and a pure sampling function. It then needs no delay primitive and is testable.
- [wayland] A per-element opacity tween must not run during a container fade. Two alpha ramps multiply into an unintended curve.
- [wayland] Tween only opacity on a fading, resizing surface and snap geometry at the endpoints. One tween for both rescaled the content.
- [wayland] `MarqueeTextState::marqueeing` means scrolling, not overflowing. Clip on texture width over box width.
- [wayland] Vary a cell-snapped column's fall rate by skipping ticks with a fractional accumulator. A fractional step desnaps glyphs from the grid.
