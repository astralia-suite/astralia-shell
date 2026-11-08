# Structure and refactoring

Habits that keep shared code honest during refactors, plus the wall-clock and drawing rules every view relies on. Sources: `src/core/wall_clock.cpp`, `src/core/signal.h`, `src/render/sticker.cpp`, `src/render/palette_extract.cpp`, `src/render/canvas.h`.

## Refactoring

- Guard a service-signal slot with the owner's alive flag. `Signal` has no disconnect, so a destroyed owner would be called.
- Delete a removed UI feature's click kinds, state and handlers in the same change. Leftovers otherwise read as live code.
- Migrate every call site when a refactor changes a shared function's contract. Stragglers silently skip the new behaviour.
- Move call sites to the new names instead of keeping alias headers. Aliases hide which file a symbol comes from.
- Size a per-enum table to its enum. A too-short table silently zero-fills.

## Clocks

- Read wall time from `system_clock`, not `std::time`. The latter is tick-coarse and lags the second boundary.
- Schedule second timers on the wall-clock boundary. A free-running one-second timer drifts away from other clocks.

## Drawing

- Skip a sticker ring when the box cannot hold it. A thin track is swallowed by border plus ring.
- Keep border, ring and shadow colours distinct in dark schemes. Adjacent near-black layers merge into one blob.
- A canvas draws images at their natural size. Scale to the slot yourself, or a large icon covers its surroundings.
- Tint photos, icons and gauges with white. A coloured tint recolours or dims the image; only monochrome masks take a colour.
- Score colour clusters by `sqrt(area) * chroma`. A linear area score let the pale majority hide small vivid regions.
- Overlapping translucent shapes of one colour double-darken. Wayland draws them opaque over an opaque base; x11 paints outer borders, then `CAIRO_OPERATOR_SOURCE` inner fills.
