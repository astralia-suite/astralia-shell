# Plan: one drawing API on Wayland, one image decoder

Goal: every Wayland module draws through `ui::Canvas` (`GlCanvas`), the scene graph is deleted, and both backends share one image decoder. X11 keeps Cairo, Cairo keeps no animation.

## Findings that shape the plan

- `GlCanvas` records into a `Scene` of `Node`s and draws it at `flush()`. Nothing outside `node.cpp` calls `node_tree_dirty`/`Scene::dirty()`, and every frame is a full redraw, so the tree only delays the draw.
- Direct `Node` users: `lock/wayland/card.cpp` (54 uses), `bar/wayland/frame.cpp` (`punch`), `wallpaper` (`VideoTexture`), `overview` (rounded texture), `idle`, `rain`, `dashboard`, `animated_image`, `arc_gauge`, `module_registry` (wallpaper), `wayland/render/text_field`.
- `panel_chrome.{h,cpp}` (364 lines) is dead apart from `cached_text`, which only the Wayland `text_field` uses, which only the lock card uses.
- `rotation` is used once (`card.cpp:608`); `punch` only in the bar frame.
- `GlCanvas::set_opacity` applies to the whole canvas at `flush()`. Four hosts (launcher, overview, logout, settings) call it after painting.
- Image decoding exists twice: Wayland `image.cpp` (libpng, libjpeg, librsvg) and X11 `image_decode.cpp` (stb_image, resvg, libjpeg).

## Phase 1: `GlCanvas` draws immediately; scene graph removed

- [MODIFY] `backend/wayland/render/gl_canvas.{h,cpp}`: each call draws through `Renderer` right away. Groups become `push_model` + `ScopedClip`-style clip stack; `set_opacity` sets `Renderer` opacity for later draws. Delete `Scene`, `stack_` of nodes, the colour `palette_` blocks and `group()`.
- [MODIFY] `gl_canvas.h`: add Wayland-only extras for hooks: `texture(const Texture &, Box, tint, radius = 0)`, `video(const VideoTexture &, Box)`, `erase(bool)` (the old `punch`), `renderer()` for `draw_custom` users.
- [MODIFY] launcher, overview, logout, settings hosts: call `set_opacity` before painting.
- [MODIFY] `bar/wayland/{bar,frame}.{h,cpp}`: frame draws through `GlCanvas` with `erase(true)` in place of `punch`.
- [MODIFY] `overview/wayland/overview.cpp`: `canvas.texture(..., radius)`.
- [MODIFY] `animated_image.{h,cpp}`: `animated_image_draw(img, GlCanvas &, ...)`.
- [MODIFY] `arc_gauge.{h,cpp}`: drop the `Node` include if only used for types.
- [MODIFY] `idle`, `rain`, `dashboard`, `wallpaper` hosts and `app/module_registry.cpp`: own a `GlCanvas` instead of a `Scene`; wallpaper columns use `canvas.video` / `canvas.texture` inside a clipped group.
- [DELETE] `backend/wayland/render/node.{h,cpp}`, `scene.h`.

## Phase 2: Wayland lock on the shared view

- [MODIFY] `modules/lock/wayland/lock.{h,cpp}`: paint with `paint_lock(canvas, model, info, w, h)` like X11, avatar through an `AnimatedImage` hook.
- [MODIFY] `modules/lock/view.{h,cpp}`: add a `LockArt` hook for the avatar (same pattern as `LogoutLogoPainter`); password field uses the shared `FieldView`.
- [DELETE] `modules/lock/wayland/card.{h,cpp}`, `backend/wayland/render/text_field.{h,cpp}`, `backend/wayland/render/panel_chrome.{h,cpp}`.
- [MODIFY] `modules/lock/model.{h,cpp}`: own the panel rotation and row slide targets the card animated (see Decisions 1).
- [MODIFY] `render/canvas.h`, `backend/wayland/render/gl_canvas.cpp`: `GroupOptions::rotation`, applied with `Renderer::push_model`.

## Phase 3: one image decoder

- [NEW] `src/render/image_decode.{h,cpp}`: stb_image + resvg + libjpeg (with `scale_denom`) into an RGBA buffer, plus `cover` and `jpeg_reduction`. Display-free, so it sits beside the other shared render code and is testable headless.
- [MODIFY] `backend/x11/render/image_decode.{h,cpp}`: keep only the Cairo wrap (in-place premultiply into an image surface), `surface_opaque` and `write_jpeg`.
- [MODIFY] `backend/wayland/render/image.{h,cpp}`: keep only the texture upload over the shared decoder.
- [MODIFY] `meson.build`, `backend/wayland/meson.build`, `build.sh`: Wayland drops `librsvg-2.0` and `libpng`; `resvg` and `stb` move to the shared deps.
- [MODIFY] `backend/wayland/render/icon.cpp`: rasterize icons and the YujiMai glyph through Pango with the icon font options; drop the direct FreeType face loading (Decisions 2).
- [MODIFY] `backend/x11/render/text.cpp`, `cairo_canvas.cpp`: give icon layouts the same font options and fractional ink placement.
- [MODIFY] `test/backend/wayland/render/test_image_decode.cpp`: point at the shared decoder (SVG aspect, JPEG reduction).

## Phase 4: docs

- [MODIFY] `important/index.md`, `important/convention.md`, `important/knowledge.md`: remove the scene graph, `card`, `panel_chrome`; state that Wayland modules draw only through `GlCanvas` and that `set_opacity` affects later draws. Record the design here instead of a new `system_architecture.md`.

## Verification (each phase)

- `./build.sh test` (meson compile + `meson test`: unit, wayland-unit, x11-unit, backend-smoke).
- Run the Wayland session by hand for the bar fillets, wallpaper video and transitions, lock, logout, idle, rain; X11 for lock, wallpaper thumbnails and the launcher.

## Expected result

- About 1.5–2k lines removed (node/scene ~255, panel_chrome ~365, Wayland text_field ~110, lock card ~650, duplicate decoder ~200, palette blocks), and two Wayland dependencies (`librsvg`, `libpng`, plus gdk-pixbuf pulled in by librsvg).
- That is ~4% of the code base. The larger duplication left is per-module Wayland surface and EGL boilerplate (`gl_make_current`, `begin_frame`, clear, swap in every host); not in this plan.

## Decisions

1. Lock (Phase 2): the Wayland card's animations (panel rotation, password row slide) move into the shared lock model and view as `AnimationManager` targets. X11 keeps none, because it runs with `animation_set_instant(true)`. `GroupOptions` gains `rotation`; `CairoCanvas` ignores it.
2. Icons (Phase 3, pending answer): render icons with Pango on both backends, using the current Wayland icon font options (no hinting, grey antialiasing, metrics off) and fractional ink extents. Wayland icons stay pixel-identical, X11 icons gain the same thick, unhinted strokes, and `icon.cpp`'s FreeType and UTF-8 code goes.
