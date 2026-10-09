# Task: one drawing API on Wayland, one image decoder

See `implementation_plan.md`.

## Phase 1: immediate `GlCanvas`, scene graph removed
- [x] Baseline build and tests
- [x] `GlCanvas` draws through `Renderer`; extras `texture`, `video`, `erase`, `renderer()`
- [x] Opacity set before painting (launcher, overview, logout, settings)
- [x] Bar frame on `GlCanvas`
- [x] Overview texture
- [x] `animated_image_draw` on `GlCanvas`
- [x] Idle, rain, dashboard, wallpaper, module_registry on `GlCanvas`
- [x] Lock card deleted directly (phases 1 and 2 built together)
- [x] Delete `node.{h,cpp}`, `scene.h`
- [x] Build and tests

## Phase 2: Wayland lock on the shared view
- [x] Lock model owns rotation and row slide
- [x] `GroupOptions::rotation`
- [x] `LockArt` avatar hook
- [x] Wayland lock host paints `paint_lock`
- [x] Delete card, Wayland text_field, panel_chrome, text_elide, dead text/icon helpers
- [x] Build and tests

## Phase 3: one image decoder, Pango icons
- [x] Shared `render/image_decode`
- [x] X11 and Wayland wrappers over it
- [x] Pango icons on both backends
- [x] Dependencies trimmed (librsvg, libpng, freetype2 off Wayland)
- [x] Build and tests

## Phase 4: docs
- [x] `index.md`, `convention.md`, `knowledge.md`
