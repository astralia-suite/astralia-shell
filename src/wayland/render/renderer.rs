//! Shape and texture renderer over the shared OpenGL ES 2.0 context.

use glow::HasContext;

/// Rounded shapes pad their quad so the antialiased edge is never clipped.
const EDGE_PAD: f32 = 2.0;
const QUAD: [f32; 8] = [0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 1.0];

macro_rules! shader {
    ($name:literal) => {
        include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/assets/shaders/renderer/", $name))
    };
}

/// A 2D affine transform: `x' = a x + b y + tx`, `y' = c x + d y + ty`.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Affine2D {
    pub a: f32,
    pub b: f32,
    pub c: f32,
    pub d: f32,
    pub tx: f32,
    pub ty: f32,
}

impl Default for Affine2D {
    fn default() -> Self {
        Affine2D { a: 1.0, b: 0.0, c: 0.0, d: 1.0, tx: 0.0, ty: 0.0 }
    }
}

impl Affine2D {
    pub fn translation(x: f32, y: f32) -> Self {
        Affine2D { tx: x, ty: y, ..Default::default() }
    }

    pub fn scaling(s: f32) -> Self {
        Affine2D { a: s, d: s, ..Default::default() }
    }

    pub fn rotation_deg(deg: f32) -> Self {
        let (sn, cs) = deg.to_radians().sin_cos();
        Affine2D { a: cs, b: -sn, c: sn, d: cs, tx: 0.0, ty: 0.0 }
    }

    /// `self` applied after `l`.
    pub fn compose(&self, l: &Affine2D) -> Self {
        Affine2D {
            a: self.a * l.a + self.b * l.c,
            b: self.a * l.b + self.b * l.d,
            c: self.c * l.a + self.d * l.c,
            d: self.c * l.b + self.d * l.d,
            tx: self.a * l.tx + self.b * l.ty + self.tx,
            ty: self.c * l.tx + self.d * l.ty + self.ty,
        }
    }
}

/// A texture in device pixels; it draws at `width / scale` by `height / scale` logical pixels.
#[derive(Debug)]
pub struct Texture {
    pub id: glow::Texture,
    pub width: i32,
    pub height: i32,
    pub scale: i32,
}

pub struct Renderer {
    gl: glow::Context,
    rect: glow::Program,
    tex: glow::Program,
    rrect: glow::Program,
    rounded_tex: glow::Program,
    fillet: glow::Program,
    polygon: glow::Program,
    polygon_tex: glow::Program,
    quad_vbo: glow::Buffer,
    polygon_vbo: glow::Buffer,
    viewport: [f32; 2],
    scale: i32,
    opacity: f32,
    model_stack: Vec<Affine2D>,
    /// Logical `[x0, y0, x1, y1]`, each already intersected with its parent.
    clip_stack: Vec<[f32; 4]>,
}

fn compile(gl: &glow::Context, fs_src: &str, label: &str) -> Result<glow::Program, String> {
    compile_with(gl, shader!("quad.vert"), fs_src, label)
}

/// Compiles and links a program; attribute 0 is `a_pos` (or `aPos`), attribute 1 `a_uv`.
pub fn compile_with(gl: &glow::Context, vs_src: &str, fs_src: &str, label: &str) -> Result<glow::Program, String> {
    // SAFETY: called with the context current.
    unsafe {
        let stage = |kind: u32, src: &str| -> Result<glow::Shader, String> {
            let shader = gl.create_shader(kind)?;
            gl.shader_source(shader, src);
            gl.compile_shader(shader);
            if !gl.get_shader_compile_status(shader) {
                let info = gl.get_shader_info_log(shader);
                gl.delete_shader(shader);
                ::log::error!("gl: shader compile failed ({label}): {info}");
                return Err(format!("gl: shader compile failed ({label})"));
            }
            Ok(shader)
        };
        let vs = stage(glow::VERTEX_SHADER, vs_src)?;
        let fs = match stage(glow::FRAGMENT_SHADER, fs_src) {
            Ok(fs) => fs,
            Err(e) => {
                gl.delete_shader(vs);
                return Err(e);
            }
        };
        let program = gl.create_program()?;
        gl.attach_shader(program, vs);
        gl.attach_shader(program, fs);
        gl.bind_attrib_location(program, 0, "a_pos");
        gl.bind_attrib_location(program, 0, "aPos");
        gl.bind_attrib_location(program, 1, "a_uv");
        gl.link_program(program);
        gl.delete_shader(vs);
        gl.delete_shader(fs);
        if !gl.get_program_link_status(program) {
            ::log::error!("gl: program link failed ({label}): {}", gl.get_program_info_log(program));
            gl.delete_program(program);
            return Err(format!("gl: program link failed ({label})"));
        }
        Ok(program)
    }
}

fn bytes(v: &[f32]) -> Vec<u8> {
    v.iter().flat_map(|f| f.to_ne_bytes()).collect()
}

impl Renderer {
    pub fn new(gl: glow::Context) -> Result<Renderer, String> {
        let rect = compile(&gl, shader!("rect.frag"), "rect")?;
        let tex = compile(&gl, shader!("tex.frag"), "tex")?;
        let rrect = compile(&gl, shader!("rrect.frag"), "rrect")?;
        let rounded_tex = compile(&gl, shader!("rounded_tex.frag"), "rounded_tex")?;
        let fillet = compile(&gl, shader!("fillet.frag"), "fillet")?;
        let polygon = compile_with(&gl, shader!("polygon.vert"), shader!("rect.frag"), "polygon")?;
        let polygon_tex = compile_with(&gl, shader!("polygon_tex.vert"), shader!("tex.frag"), "polygon_tex")?;
        // SAFETY: the context is current.
        let (quad_vbo, polygon_vbo) = unsafe {
            let vbo = gl.create_buffer()?;
            gl.bind_buffer(glow::ARRAY_BUFFER, Some(vbo));
            gl.buffer_data_u8_slice(glow::ARRAY_BUFFER, &bytes(&QUAD), glow::STATIC_DRAW);
            (vbo, gl.create_buffer()?)
        };
        Ok(Renderer {
            gl,
            rect,
            tex,
            rrect,
            rounded_tex,
            fillet,
            polygon,
            polygon_tex,
            quad_vbo,
            polygon_vbo,
            viewport: [1.0, 1.0],
            scale: 1,
            opacity: 1.0,
            model_stack: vec![Affine2D::default()],
            clip_stack: Vec::new(),
        })
    }

    #[cfg_attr(not(test), expect(dead_code))]
    pub fn gl(&self) -> &glow::Context {
        &self.gl
    }

    pub fn begin_frame(&mut self, logical_w: i32, logical_h: i32, scale: i32) {
        self.viewport = [logical_w as f32, logical_h as f32];
        self.scale = scale.max(1);
        self.opacity = 1.0;
        self.model_stack.truncate(1);
        self.model_stack[0] = Affine2D::default();
        self.clip_stack.clear();
        // SAFETY: the context is current.
        unsafe {
            self.gl.viewport(0, 0, logical_w * self.scale, logical_h * self.scale);
            self.gl.disable(glow::SCISSOR_TEST);
            self.gl.enable(glow::BLEND);
        }
        self.set_erase_blend(false);
    }

    #[cfg_attr(not(test), expect(dead_code))]
    pub fn scale(&self) -> i32 {
        self.scale
    }

    /// Multiplied into the alpha of every fill and tint until the next `begin_frame`.
    pub fn set_opacity(&mut self, a: f32) {
        self.opacity = a;
    }

    /// While on, drawing removes coverage from the target instead of painting.
    pub fn set_erase_blend(&self, on: bool) {
        // SAFETY: the context is current.
        unsafe {
            if on {
                self.gl.blend_func(glow::ZERO, glow::ONE_MINUS_SRC_ALPHA);
            } else {
                self.gl.blend_func_separate(glow::SRC_ALPHA, glow::ONE_MINUS_SRC_ALPHA, glow::ONE, glow::ONE_MINUS_SRC_ALPHA);
            }
        }
    }

    pub fn push_model(&mut self, local: &Affine2D) {
        let top = self.model_stack.last().copied().unwrap_or_default();
        self.model_stack.push(top.compose(local));
    }

    pub fn pop_model(&mut self) {
        if self.model_stack.len() > 1 {
            self.model_stack.pop();
        }
    }

    /// Pushes a clip intersected with the current one.
    pub fn set_clip(&mut self, x: f32, y: f32, w: f32, h: f32) {
        let mut r = [x, y, x + w, y + h];
        if let Some(p) = self.clip_stack.last() {
            r = [r[0].max(p[0]), r[1].max(p[1]), r[2].min(p[2]), r[3].min(p[3])];
        }
        self.clip_stack.push(r);
        self.apply_clip(r);
    }

    pub fn clear_clip(&mut self) {
        self.clip_stack.pop();
        match self.clip_stack.last() {
            Some(&r) => self.apply_clip(r),
            // SAFETY: the context is current.
            None => unsafe { self.gl.disable(glow::SCISSOR_TEST) },
        }
    }

    /// Each edge rounds outward on its own, so a fractional clip never cuts a pixel it touches.
    fn apply_clip(&self, r: [f32; 4]) {
        let s = self.scale as f32;
        let x0 = (r[0] * s).floor() as i32;
        let x1 = (r[2] * s).ceil() as i32;
        let y0 = ((self.viewport[1] - r[3]) * s).floor() as i32;
        let y1 = ((self.viewport[1] - r[1]) * s).ceil() as i32;
        // SAFETY: the context is current.
        unsafe {
            self.gl.enable(glow::SCISSOR_TEST);
            self.gl.scissor(x0, y0, (x1 - x0).max(0), (y1 - y0).max(0));
        }
    }

    fn faded(&self, c: [f32; 4]) -> [f32; 4] {
        [c[0], c[1], c[2], c[3] * self.opacity]
    }

    pub fn clear(&self) {
        // SAFETY: the context is current.
        unsafe {
            self.gl.clear_color(0.0, 0.0, 0.0, 0.0);
            self.gl.clear(glow::COLOR_BUFFER_BIT);
        }
    }

    /// Uses `program` and sets `u_viewport`, `u_rect` and the identity model.
    fn bind(&self, program: glow::Program, x: f32, y: f32, w: f32, h: f32) {
        let gl = &self.gl;
        // SAFETY: the context is current.
        unsafe {
            gl.use_program(Some(program));
            gl.uniform_2_f32(gl.get_uniform_location(program, "u_viewport").as_ref(), self.viewport[0], self.viewport[1]);
            gl.uniform_4_f32(gl.get_uniform_location(program, "u_rect").as_ref(), x, y, w, h);
            let m = self.model_stack.last().copied().unwrap_or_default();
            gl.uniform_4_f32(gl.get_uniform_location(program, "u_model_ab").as_ref(), m.a, m.b, m.c, m.d);
            gl.uniform_2_f32(gl.get_uniform_location(program, "u_model_t").as_ref(), m.tx, m.ty);
        }
    }

    fn set_vec4(&self, program: glow::Program, name: &str, v: [f32; 4]) {
        // SAFETY: the context is current.
        unsafe { self.gl.uniform_4_f32_slice(self.gl.get_uniform_location(program, name).as_ref(), &v) };
    }

    fn set_f32(&self, program: glow::Program, name: &str, v: f32) {
        // SAFETY: the context is current.
        unsafe { self.gl.uniform_1_f32(self.gl.get_uniform_location(program, name).as_ref(), v) };
    }

    fn set_size(&self, program: glow::Program, w: f32, h: f32) {
        // SAFETY: the context is current.
        unsafe { self.gl.uniform_2_f32(self.gl.get_uniform_location(program, "u_size").as_ref(), w, h) };
    }

    fn bind_texture(&self, program: glow::Program, tex: &Texture) {
        // SAFETY: the context is current.
        unsafe {
            self.gl.active_texture(glow::TEXTURE0);
            self.gl.bind_texture(glow::TEXTURE_2D, Some(tex.id));
            self.gl.uniform_1_i32(self.gl.get_uniform_location(program, "u_tex").as_ref(), 0);
        }
    }

    fn draw_quad(&self) {
        let gl = &self.gl;
        // SAFETY: the context is current; the VBO holds `QUAD`.
        unsafe {
            gl.bind_buffer(glow::ARRAY_BUFFER, Some(self.quad_vbo));
            gl.enable_vertex_attrib_array(0);
            gl.vertex_attrib_pointer_f32(0, 2, glow::FLOAT, false, 0, 0);
            gl.draw_arrays(glow::TRIANGLE_STRIP, 0, 4);
            gl.disable_vertex_attrib_array(0);
        }
    }

    /// `color` is straight alpha.
    pub fn draw_rect(&self, x: f32, y: f32, w: f32, h: f32, color: [f32; 4]) {
        self.bind(self.rect, x, y, w, h);
        self.set_vec4(self.rect, "u_color", self.faded(color));
        self.draw_quad();
    }

    #[allow(clippy::too_many_arguments)]
    pub fn draw_rounded_rect(&self, x: f32, y: f32, w: f32, h: f32, radius: f32, border_width: f32, fill: [f32; 4], border: [f32; 4]) {
        let p = self.rrect;
        self.bind(p, x - EDGE_PAD, y - EDGE_PAD, w + 2.0 * EDGE_PAD, h + 2.0 * EDGE_PAD);
        self.set_size(p, w, h);
        self.set_f32(p, "u_pad", EDGE_PAD);
        self.set_f32(p, "u_radius", radius);
        self.set_f32(p, "u_border_width", border_width);
        self.set_vec4(p, "u_fill_color", self.faded(fill));
        self.set_vec4(p, "u_border_color", self.faded(border));
        self.draw_quad();
    }

    /// A `size` square minus the circle of radius `size` centred on its bottom-left or bottom-right corner.
    pub fn draw_fillet(&self, x: f32, y: f32, size: f32, circle_on_right: bool, color: [f32; 4]) {
        let p = self.fillet;
        self.bind(p, x - EDGE_PAD, y - EDGE_PAD, size + 2.0 * EDGE_PAD, size + 2.0 * EDGE_PAD);
        self.set_f32(p, "u_size", size);
        self.set_f32(p, "u_pad", EDGE_PAD);
        self.set_f32(p, "u_circle_right", if circle_on_right { 1.0 } else { 0.0 });
        self.set_vec4(p, "u_color", self.faded(color));
        self.draw_quad();
    }

    /// Uploads straight-alpha RGBA in device pixels.
    pub fn upload_rgba(&self, w: i32, h: i32, scale: i32, rgba: &[u8]) -> Option<Texture> {
        self.upload(w, h, scale, rgba, false)
    }

    /// As `upload_rgba`, with mipmaps for images drawn smaller than their size.
    pub fn upload_rgba_mipmapped(&self, w: i32, h: i32, scale: i32, rgba: &[u8]) -> Option<Texture> {
        self.upload(w, h, scale, rgba, true)
    }

    fn upload(&self, w: i32, h: i32, scale: i32, rgba: &[u8], mipmapped: bool) -> Option<Texture> {
        if w <= 0 || h <= 0 || rgba.len() < (w * h * 4) as usize {
            return None;
        }
        let gl = &self.gl;
        // SAFETY: the context is current; `rgba` holds `w * h` pixels.
        unsafe {
            let id = gl.create_texture().ok()?;
            gl.bind_texture(glow::TEXTURE_2D, Some(id));
            let min = if mipmapped { glow::LINEAR_MIPMAP_LINEAR } else { glow::LINEAR };
            gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_MIN_FILTER, min as i32);
            gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_MAG_FILTER, glow::LINEAR as i32);
            gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_WRAP_S, glow::CLAMP_TO_EDGE as i32);
            gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_WRAP_T, glow::CLAMP_TO_EDGE as i32);
            gl.tex_image_2d(glow::TEXTURE_2D, 0, glow::RGBA as i32, w, h, 0, glow::RGBA, glow::UNSIGNED_BYTE, glow::PixelUnpackData::Slice(Some(rgba)));
            if mipmapped {
                gl.generate_mipmap(glow::TEXTURE_2D);
            }
            Some(Texture { id, width: w, height: h, scale: scale.max(1) })
        }
    }

    #[cfg_attr(not(test), expect(dead_code))]
    pub fn draw_texture(&self, x: f32, y: f32, tex: &Texture, tint: [f32; 4]) {
        let scale = tex.scale.max(1) as f32;
        self.draw_texture_rect(x, y, tex.width as f32 / scale, tex.height as f32 / scale, tex, tint);
    }

    pub fn draw_texture_rect(&self, x: f32, y: f32, w: f32, h: f32, tex: &Texture, tint: [f32; 4]) {
        let p = self.tex;
        self.bind(p, x.round(), y.round(), w, h);
        self.bind_texture(p, tex);
        self.set_vec4(p, "u_color", self.faded(tint));
        self.draw_quad();
    }

    #[allow(clippy::too_many_arguments)]
    pub fn draw_texture_rect_rounded(&self, x: f32, y: f32, w: f32, h: f32, radius: f32, tex: &Texture, tint: [f32; 4]) {
        let (x, y) = (x.round(), y.round());
        let p = self.rounded_tex;
        self.bind(p, x - EDGE_PAD, y - EDGE_PAD, w + 2.0 * EDGE_PAD, h + 2.0 * EDGE_PAD);
        self.set_f32(p, "u_pad", EDGE_PAD);
        self.bind_texture(p, tex);
        self.set_vec4(p, "u_color", self.faded(tint));
        self.set_size(p, w, h);
        self.set_f32(p, "u_radius", radius);
        self.draw_quad();
    }

    /// Compiles `fs_src` against the shared quad vertex shader.
    pub fn compile_program(&self, fs_src: &str, label: &str) -> Result<glow::Program, String> {
        compile(&self.gl, fs_src, label)
    }

    /// Draws the quad `(x, y, w, h)` with `program` after `set` has set its own uniforms and textures.
    pub fn draw_custom(&self, program: glow::Program, x: f32, y: f32, w: f32, h: f32, set: impl FnOnce(&glow::Context, glow::Program)) {
        self.bind(program, x, y, w, h);
        set(&self.gl, program);
        self.draw_quad();
        // SAFETY: the context is current.
        unsafe { self.gl.active_texture(glow::TEXTURE0) };
    }

    /// Uploads RGBA sampled with nearest filtering, for data maps that must not blend texels.
    pub fn upload_rgba_nearest(&self, w: i32, h: i32, rgba: &[u8]) -> Option<Texture> {
        let tex = self.upload(w, h, 1, rgba, false)?;
        // SAFETY: the context is current; `tex` was just created.
        unsafe {
            self.gl.bind_texture(glow::TEXTURE_2D, Some(tex.id));
            self.gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_MIN_FILTER, glow::NEAREST as i32);
            self.gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_MAG_FILTER, glow::NEAREST as i32);
            self.gl.bind_texture(glow::TEXTURE_2D, None);
        }
        Some(tex)
    }

    pub fn delete_program(&self, program: glow::Program) {
        // SAFETY: the context is current; the caller drops its copy of the id.
        unsafe { self.gl.delete_program(program) };
    }

    /// Replaces the pixels of `tex` with `rgba` of the same size.
    pub fn update_rgba(&self, tex: &Texture, rgba: &[u8]) {
        if rgba.len() < (tex.width * tex.height * 4) as usize {
            return;
        }
        // SAFETY: the context is current; `rgba` holds `tex.width * tex.height` pixels.
        unsafe {
            self.gl.bind_texture(glow::TEXTURE_2D, Some(tex.id));
            self.gl.tex_sub_image_2d(glow::TEXTURE_2D, 0, 0, 0, tex.width, tex.height, glow::RGBA, glow::UNSIGNED_BYTE, glow::PixelUnpackData::Slice(Some(rgba)));
            self.gl.bind_texture(glow::TEXTURE_2D, None);
        }
    }

    /// A convex fan through `xy` (`x, y` pairs in logical pixels); `color` is straight alpha.
    pub fn draw_polygon(&self, xy: &[f32], color: [f32; 4]) {
        let count = (xy.len() / 2) as i32;
        if count < 3 {
            return;
        }
        self.bind(self.polygon, 0.0, 0.0, 0.0, 0.0);
        self.set_vec4(self.polygon, "u_color", self.faded(color));
        let gl = &self.gl;
        // SAFETY: the context is current; the buffer holds `count` vertices of two floats.
        unsafe {
            gl.bind_buffer(glow::ARRAY_BUFFER, Some(self.polygon_vbo));
            gl.buffer_data_u8_slice(glow::ARRAY_BUFFER, &bytes(xy), glow::STREAM_DRAW);
            gl.enable_vertex_attrib_array(0);
            gl.vertex_attrib_pointer_f32(0, 2, glow::FLOAT, false, 0, 0);
            gl.draw_arrays(glow::TRIANGLE_FAN, 0, count);
            gl.disable_vertex_attrib_array(0);
        }
    }

    /// A convex fan through `xy_uv` (`x, y, u, v` per vertex) sampling `tex`.
    pub fn draw_textured_polygon(&self, xy_uv: &[f32], tex: &Texture, tint: [f32; 4]) {
        let count = (xy_uv.len() / 4) as i32;
        if count < 3 {
            return;
        }
        self.bind(self.polygon_tex, 0.0, 0.0, 0.0, 0.0);
        self.bind_texture(self.polygon_tex, tex);
        self.set_vec4(self.polygon_tex, "u_color", self.faded(tint));
        let gl = &self.gl;
        let stride = 4 * std::mem::size_of::<f32>() as i32;
        // SAFETY: the context is current; the buffer holds `count` vertices of four floats.
        unsafe {
            gl.bind_buffer(glow::ARRAY_BUFFER, Some(self.polygon_vbo));
            gl.buffer_data_u8_slice(glow::ARRAY_BUFFER, &bytes(xy_uv), glow::STREAM_DRAW);
            gl.enable_vertex_attrib_array(0);
            gl.vertex_attrib_pointer_f32(0, 2, glow::FLOAT, false, stride, 0);
            gl.enable_vertex_attrib_array(1);
            gl.vertex_attrib_pointer_f32(1, 2, glow::FLOAT, false, stride, 2 * std::mem::size_of::<f32>() as i32);
            gl.draw_arrays(glow::TRIANGLE_FAN, 0, count);
            gl.disable_vertex_attrib_array(0);
            gl.disable_vertex_attrib_array(1);
        }
    }

    /// Copies the current framebuffer into `tex`, creating it on first use.
    pub fn grab_framebuffer(&self, tex: &mut Option<Texture>) {
        let width = self.viewport[0] as i32 * self.scale;
        let height = self.viewport[1] as i32 * self.scale;
        if width <= 0 || height <= 0 {
            return;
        }
        let gl = &self.gl;
        // SAFETY: the context is current.
        unsafe {
            if tex.is_none() {
                let Ok(id) = gl.create_texture() else {
                    ::log::error!("gl: cannot create snapshot texture");
                    return;
                };
                gl.bind_texture(glow::TEXTURE_2D, Some(id));
                gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_MIN_FILTER, glow::LINEAR as i32);
                gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_MAG_FILTER, glow::LINEAR as i32);
                gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_WRAP_S, glow::CLAMP_TO_EDGE as i32);
                gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_WRAP_T, glow::CLAMP_TO_EDGE as i32);
                *tex = Some(Texture { id, width: 0, height: 0, scale: 1 });
            }
            let Some(t) = tex.as_mut() else { return };
            gl.bind_texture(glow::TEXTURE_2D, Some(t.id));
            gl.copy_tex_image_2d(glow::TEXTURE_2D, 0, glow::RGBA, 0, 0, width, height, 0);
            gl.bind_texture(glow::TEXTURE_2D, None);
            t.width = width;
            t.height = height;
            t.scale = self.scale;
        }
    }

    pub fn delete_texture(&self, tex: Texture) {
        // SAFETY: the context is current; `tex` is consumed, so the id is never reused.
        unsafe { self.gl.delete_texture(tex.id) };
    }
}

/// A surfaceless context rendering into a `w` by `h` texture; `None` (logged) where EGL is unavailable.
#[cfg(test)]
pub fn offscreen(w: i32, h: i32) -> Option<(super::egl::Egl, Renderer)> {
    let egl = match super::egl::Egl::surfaceless() {
        Ok(egl) => egl,
        Err(e) => {
            println!("skipped: {e}");
            return None;
        }
    };
    assert!(egl.make_current(None));
    let r = Renderer::new(egl.gl()).expect("renderer");
    let gl = r.gl();
    // SAFETY: the context is current on this thread.
    unsafe {
        let target = gl.create_texture().unwrap();
        gl.bind_texture(glow::TEXTURE_2D, Some(target));
        gl.tex_image_2d(glow::TEXTURE_2D, 0, glow::RGBA as i32, w, h, 0, glow::RGBA, glow::UNSIGNED_BYTE, glow::PixelUnpackData::Slice(None));
        let fbo = gl.create_framebuffer().unwrap();
        gl.bind_framebuffer(glow::FRAMEBUFFER, Some(fbo));
        gl.framebuffer_texture_2d(glow::FRAMEBUFFER, glow::COLOR_ATTACHMENT0, glow::TEXTURE_2D, Some(target), 0);
        assert_eq!(gl.check_framebuffer_status(glow::FRAMEBUFFER), glow::FRAMEBUFFER_COMPLETE);
    }
    Some((egl, r))
}

/// The RGBA pixel at logical `(x, y)` of an offscreen target `h` pixels tall.
#[cfg(test)]
pub fn read_pixel(r: &Renderer, h: i32, x: i32, y: i32) -> [u8; 4] {
    let mut px = [0u8; 4];
    // SAFETY: the context is current; `px` holds one RGBA pixel.
    unsafe { r.gl().read_pixels(x, h - 1 - y, 1, 1, glow::RGBA, glow::UNSIGNED_BYTE, glow::PixelPackData::Slice(Some(&mut px))) };
    px
}

#[cfg(test)]
mod tests {
    use super::*;

    const W: i32 = 64;
    const H: i32 = 32;

    fn assert_near(got: [u8; 4], want: [u8; 4]) {
        assert!(got.iter().zip(want).all(|(&g, w)| g.abs_diff(w) <= 2), "got {got:?}, want {want:?}");
    }

    #[test]
    fn renderer_draws_shapes() {
        let Some((_egl, mut r)) = offscreen(W, H) else { return };
        r.begin_frame(W, H, 1);
        r.clear();
        let pixel = |r: &Renderer, x: i32, y: i32| read_pixel(r, H, x, y);

        r.draw_rect(0.0, 0.0, 16.0, 16.0, [1.0, 0.0, 0.0, 0.5]);
        assert_near(pixel(&r, 8, 8), [128, 0, 0, 128]);
        r.draw_rounded_rect(24.0, 0.0, 32.0, 32.0, 12.0, 0.0, [0.0, 1.0, 0.0, 1.0], [0.0; 4]);
        assert_near(pixel(&r, 40, 16), [0, 255, 0, 255]);
        assert_eq!(pixel(&r, 24, 0)[3], 0);
        let tex = r.upload_rgba(2, 2, 1, &[255; 16]).unwrap();
        r.draw_texture(60.0, 28.0, &tex, [0.0, 0.0, 1.0, 1.0]);
        assert_near(pixel(&r, 61, 29), [0, 0, 255, 255]);
        r.delete_texture(tex);
        assert_eq!(r.scale(), 1);

        r.clear();
        let red = [1.0, 0.0, 0.0, 1.0];
        // Nested clips intersect with their parent.
        r.set_clip(8.0, 8.0, 32.0, 32.0);
        r.set_clip(0.0, 0.0, 16.0, 16.0);
        r.draw_rect(0.0, 0.0, 32.0, 32.0, red);
        r.clear_clip();
        r.clear_clip();
        assert_eq!(pixel(&r, 4, 4)[3], 0);
        assert_near(pixel(&r, 12, 12), [255, 0, 0, 255]);
        assert_eq!(pixel(&r, 20, 12)[3], 0);
        // Opacity multiplies alpha.
        r.set_opacity(0.5);
        r.draw_rect(40.0, 0.0, 8.0, 8.0, red);
        r.set_opacity(1.0);
        assert_near(pixel(&r, 44, 4), [128, 0, 0, 128]);
        // Erase blending clears what is under it.
        r.draw_rect(20.0, 20.0, 8.0, 8.0, red);
        assert_near(pixel(&r, 24, 24), [255, 0, 0, 255]);
        r.set_erase_blend(true);
        r.draw_rect(20.0, 20.0, 8.0, 8.0, red);
        r.set_erase_blend(false);
        assert_eq!(pixel(&r, 24, 24), [0, 0, 0, 0]);
        // The model transform moves geometry.
        r.push_model(&Affine2D::translation(30.0, 20.0));
        r.draw_rect(0.0, 0.0, 4.0, 4.0, red);
        r.pop_model();
        assert_near(pixel(&r, 32, 22), [255, 0, 0, 255]);
        assert_eq!(pixel(&r, 2, 2)[3], 0);
        // A fillet leaves its circle side clear.
        r.draw_fillet(48.0, 16.0, 16.0, false, red);
        assert_near(pixel(&r, 62, 17), [255, 0, 0, 255]);
        assert_eq!(pixel(&r, 49, 31)[3], 0);
        // A stretched texture fills its rect.
        let tex = r.upload_rgba(2, 2, 1, &[255; 16]).unwrap();
        r.draw_texture_rect(0.0, 24.0, 8.0, 8.0, &tex, [0.0, 0.0, 1.0, 1.0]);
        assert_near(pixel(&r, 6, 30), [0, 0, 255, 255]);
        r.delete_texture(tex);
    }

    #[test]
    fn affine_compose() {
        let m = Affine2D::translation(10.0, 5.0).compose(&Affine2D::scaling(2.0));
        assert_eq!((m.a, m.d, m.tx, m.ty), (2.0, 2.0, 10.0, 5.0));
        let r = Affine2D::rotation_deg(90.0);
        assert!((r.b + 1.0).abs() < 1e-6 && (r.c - 1.0).abs() < 1e-6);
    }
}
