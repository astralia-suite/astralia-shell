# Images, fonts and text

Glyphs, text metrics, alpha conventions and image decoding shared by both backends. Sources: `src/render/decode.cpp`, `src/render/app_fonts.cpp`, `src/render/icons.h`, `src/backend/wayland/render/text.cpp`, `src/backend/x11/render/text.cpp`.

## Fonts and text

- Tabler codepoints collide with Codicons in the Nerd Font. Name the `tabler-icons` Pango family explicitly for icons.
- Register app fonts before the first `Text` exists. Pango snapshots fonts on creation and misses ones added later.
- A glyphless icon codepoint fails silently as an empty texture. Check icon constants against the actual font.
- Size text and icon textures from fixed font metrics, not ink extents. Ink sizing made the baseline jitter between strings.
- A per-glyph text field advances each cell by the font's fixed advance. Summing ink widths collapses narrow glyphs.
- A glyph missing from the primary font shifts the whole line's baseline. Pango's fallback inflates ascent; use the em dash over `U+00B7`.
- `show_layout`'s current point is the top-left corner, not the baseline. Adding ascent doubled the offset.
- Font hinting differs between icons and text. `HINT_STYLE_NONE` keeps Tabler strokes thick while hinted text stays crisp.

## Images

- libjpeg reports errors by `longjmp`, skipping destructors. Hold buffers in plain or `volatile` pointers freed in the `setjmp` branch.
- libjpeg's default `error_exit` calls `exit()`. Install a handler that jumps back and returns `nullptr`.
- Cairo output is premultiplied alpha but the blend convention is straight alpha. Mixing them squares alpha at antialiased edges.
- A `Texture`'s size is device pixels (`logical * scale`). Divide by `scale` when laying out, or HiDPI blocks come out 2x.
- A decode resolution must be device pixels, not logical. Decoding at logical size upscaled images blurrily on scale 2.
- A pre-upload downsample derives its size from the source aspect ratio. Squashing to the box bakes in a stretch no crop undoes.
- `decode_pixels` fits an SVG inside its box keeping the aspect. Give a height-only fit a huge width bound.
- `decode_pixels` allocates every buffer with `new[]`, stb included through `STBI_MALLOC`. Each backend then adopts the buffer without a copy.
- `decode_pixels` returns SVG renders premultiplied and every other format straight. Check `Pixels::premultiplied` before converting to cairo's ARGB32.
- Give a JPEG decode a target size to use libjpeg `scale_denom`. Reduced-size decoding skips most pixels and memory.
