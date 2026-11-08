# Wayland rendering

The GLES2 renderer, its shape shaders and portable GLSL ES. Sources: `src/backend/wayland/render/renderer.cpp`, `src/backend/wayland/render/gl_canvas.cpp`, `src/backend/wayland/render/texture.cpp`, `src/backend/wayland/render/arc_gauge.cpp`, `assets/shaders/`.

## Canvas and shapes

- [wayland] Clip with scissor rects plus a corner inset, not a stencil buffer. It is correct while nothing visually touches a rounded edge.
- [wayland] A scissor clip helper needs a stack once clips nest. An unconditional disable on destruction wiped the outer clip.
- [wayland] Floor and ceil each scissor edge independently. Flooring the origin then rounding the size can drop the last row.
- [wayland] A group clips only when it is not itself scaled or rotated. Scissor rects ignore the model transform.
- [wayland] Draw a multi-shape outline as two stacked layers. The whole silhouette takes the border colour, the inset one the fill.
- [wayland] `draw_texture_rect` rounds its `x` and `y`, but shapes do not. Round the shared edge first or a texture lands a pixel off.
- [wayland] Fractional texture positions blur every glyph. `GL_LINEAR` blends edge texels at half-pixel offsets, so round before drawing.
- [wayland] `GlCanvas` draws immediately, so call order is paint order. `set_opacity()` applies to later draws only.
- [wayland] `draw_rounded_rect` always reads its border colour, even at zero width. Passing `nullptr` is a null read.
- [wayland] A shared `Renderer` resets itself in `begin_frame()`. Callers otherwise leaked stale opacity into the next paint.
- [wayland] Scale an arc gauge's stroke by the reference stroke-to-diameter ratio when it grows. A fixed stroke turns into a hairline.

## Shape shaders

- [wayland] Shape edges use a linear ramp one device pixel wide: `clamp(0.5 - d / length(vec2(dFdx(d), dFdy(d))), 0.0, 1.0)`. A `smoothstep` window reads as stair-stepping on curves.
- [wayland] Shape shaders output straight alpha, `vec4(rgb, a * coverage)`. Multiplying `rgb` by coverage as well squares the edge under `GL_SRC_ALPHA`.
- [wayland] Draw analytic shapes on a quad padded by `edge_pad` and pass the unpadded size as a uniform. A quad cut at the rect clips the ramp.
- [wayland] A rounded-rectangle distance needs the interior `min(max(q.x, q.y), 0.0)` term. Otherwise a border wider than the radius fills the rect.

## GL state

- [wayland] A fresh share context starts with `GL_BLEND` disabled. Raw GL paths enable it once per context.
- [wayland] `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` squares alpha over a transparent-cleared surface. Use `glBlendFuncSeparate` with `GL_ONE` for alpha.

## GLSL ES portability

- [wayland] Shaders are `#version 100` on one OpenGL ES 2.0 context. Both context creation sites must change together.
- [wayland] `smoothstep` needs `edge0 < edge1`. Mesa tolerates reversed edges, NVIDIA may not; invert with `1.0 - smoothstep(lo, hi, x)`.
- [wayland] Declare `precision highp float` unconditionally in fragment shaders. `mediump` is narrow on NVIDIA and overflowed a thunder shader.
- [wayland] Run a `float(x == x)` NaN guard before `clamp`. `clamp` launders `NaN` to an endpoint.
- [wayland] `pow(0.0, 0.0)` is undefined: NVIDIA returns `1.0`, Mesa Intel `NaN`. Guard bases with `max(base, 1e-4)`.
- [wayland] GLSL ES 1.00 needs compile-time loop bounds. Use a fixed `MAX_*` cap with an early `break`.
- [wayland] GLSL ES 1.00 has no `matNxM` types, array constructors or whole-array assignment. Assign per element after a bare declaration.
- [wayland] `glTexImage2D` internal format must equal the data format on ES 2.0. `GL_RGBA8` compiles but is the wrong token.
- [wayland] ES 2.0 needs `_EXT` tokens such as `GL_RED_EXT` and `GL_MAX_EXT`. Include `gl2ext.h` after `gl2.h`, which defines `GL_APIENTRY`.
- [wayland] `GL_UNPACK_ROW_LENGTH` needs the `GL_EXT_unpack_subimage` string probe. It is not a core ES 2.0 token.
- [wayland] Exact texel reads use `texture2D` with `GL_NEAREST`. Add `+0.5` only to integer indices; `gl_FragCoord` already sits at `i + 0.5`.
- [wayland] Float additive accumulation needs `GL_OES_texture_float`, `GL_EXT_color_buffer_float` and `GL_EXT_float_blend`. Probe the extension string.
- [wayland] Skip the CPU BGRA swizzle for shm captures. `GL_EXT_texture_format_BGRA8888` uploads them directly.
- [wayland] A fullscreen vertex shader emitting only `gl_Position` pairs with a fragment shader using `gl_FragCoord`. A `vUv` varying would be undefined.
- [wayland] Flatten ported multi-file shaders at authoring time. The shell has no runtime shader preprocessor.

## Passes and swap

- [wayland] A custom multi-pass shader effect bypasses `GlCanvas`. Give it its own programs and FBOs, called from the module's paint.
- [wayland] A single-pass module shader can ride `Renderer::draw_custom`. Reuse `renderer/quad.vert` and `quad_vbo_` instead of owning FBOs.
- [wayland] Create a static quad's buffer once with its program. Per-frame `glGenBuffers` is driver churn.
- [wayland] Render a per-frame multi-tap fullscreen pass to a downsampled FBO. At native resolution it stalls the poll loop.
- [wayland] Rasterize once and cache by key for any procedural drawing. Continuous inputs such as percentages need bucketing or the cache grows unbounded.
- [wayland] The default EGL swap interval blocks the poll thread on NVIDIA. `gl_make_current` sets `eglSwapInterval(display, 0)` on every bound surface.
- [wayland] Frame callbacks already pace every surface, so a zero swap interval causes no tearing. Skip the frame on any `EGL_FALSE`.
- [wayland] Never drive GL for a surface the compositor stopped compositing. A covered wallpaper would otherwise paint into the lock surface.
- [wayland] A splat accumulator must be a `GL_FLOAT` target. Hundreds of splats per pixel clamp at `1.0` in `UNORM`.
