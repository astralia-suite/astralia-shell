//! Visualizer shader sources and the GL helpers the stages share.
//!
//! The sphere and glow stages carry lygia's pnoise (MIT); the audio-pass shaders under
//! `assets/shaders/visualizer/` are adapted from GLava by jarcode-foss (GPL-3.0).
//! See `assets/shaders/NOTICE`.

use glow::HasContext;

use crate::wayland::render::renderer::compile_with;

macro_rules! src {
    ($name:literal) => {
        include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/assets/shaders/visualizer/", $name))
    };
}

pub const FULLSCREEN_VS: &str = src!("fullscreen.vert");
pub const AUDIO_PASS_FS: &str = src!("audio_pass.frag");
pub const AUDIO_GRAVITY_FS: &str = src!("audio_gravity.frag");
pub const AUDIO_AVERAGE_FS: &str = src!("audio_average.frag");
pub const AUDIO_SMOOTH_FS: &str = src!("audio_smooth.frag");
pub const BAR_FS: &str = src!("bar/bar.frag");
pub const SPHERE1_SPLAT_FS: &str = src!("sphere/sphere1_splat.frag");
pub const PRESENT_VS: &str = src!("sphere/present.vert");
pub const PRESENT_FS: &str = src!("sphere/present.frag");

pub fn sphere1_vs() -> String {
    [src!("sphere/sphere1_vert_head.glsl"), src!("sphere/lygia_pnoise.glsl"), src!("sphere/common.glsl"), src!("sphere/sphere1_vert_main.glsl")].concat()
}

pub fn sphere2_fs() -> String {
    [src!("sphere/sphere_head.glsl"), src!("sphere/common.glsl"), src!("sphere/defaults.glsl"), src!("sphere/sphere2_main.glsl")].concat()
}

pub fn glow_fs() -> String {
    [src!("sphere/glow_head.glsl"), src!("sphere/common.glsl"), src!("sphere/glow_main.glsl")].concat()
}

pub fn program(gl: &glow::Context, vs: &str, fs: &str, label: &str) -> Option<glow::Program> {
    compile_with(gl, vs, fs, label).ok()
}

const QUAD_VERTS: [f32; 18] = [-1.0, -1.0, 0.0, 1.0, -1.0, 0.0, -1.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0, -1.0, 0.0, -1.0, 1.0, 0.0];

pub fn f32_bytes(v: &[f32]) -> &[u8] {
    // SAFETY: any f32 slice is a valid byte slice of four times its length.
    unsafe { std::slice::from_raw_parts(v.as_ptr().cast(), std::mem::size_of_val(v)) }
}

/// The full-screen quad as two triangles of `vec3`.
pub fn quad_vbo(gl: &glow::Context) -> Option<glow::Buffer> {
    // SAFETY: the context is current.
    unsafe {
        let vbo = gl.create_buffer().ok()?;
        gl.bind_buffer(glow::ARRAY_BUFFER, Some(vbo));
        gl.buffer_data_u8_slice(glow::ARRAY_BUFFER, f32_bytes(&QUAD_VERTS), glow::STATIC_DRAW);
        gl.bind_buffer(glow::ARRAY_BUFFER, None);
        Some(vbo)
    }
}

pub fn draw_quad(gl: &glow::Context, vbo: glow::Buffer) {
    // SAFETY: the context is current; `vbo` holds `QUAD_VERTS`.
    unsafe {
        gl.bind_buffer(glow::ARRAY_BUFFER, Some(vbo));
        gl.vertex_attrib_pointer_f32(0, 3, glow::FLOAT, false, 0, 0);
        gl.enable_vertex_attrib_array(0);
        gl.draw_arrays(glow::TRIANGLES, 0, 6);
        gl.disable_vertex_attrib_array(0);
    }
}

pub fn set_i(gl: &glow::Context, program: glow::Program, name: &str, v: i32) {
    // SAFETY: the context is current and `program` is in use.
    unsafe { gl.uniform_1_i32(gl.get_uniform_location(program, name).as_ref(), v) }
}

pub fn set_f(gl: &glow::Context, program: glow::Program, name: &str, v: f32) {
    // SAFETY: the context is current and `program` is in use.
    unsafe { gl.uniform_1_f32(gl.get_uniform_location(program, name).as_ref(), v) }
}

pub fn set_f2(gl: &glow::Context, program: glow::Program, name: &str, x: f32, y: f32) {
    // SAFETY: the context is current and `program` is in use.
    unsafe { gl.uniform_2_f32(gl.get_uniform_location(program, name).as_ref(), x, y) }
}

pub fn set_f3(gl: &glow::Context, program: glow::Program, name: &str, v: [f32; 3]) {
    // SAFETY: the context is current and `program` is in use.
    unsafe { gl.uniform_3_f32(gl.get_uniform_location(program, name).as_ref(), v[0], v[1], v[2]) }
}

/// Binds `tex` to texture unit `unit` and points sampler `name` at it.
pub fn bind_sampler(gl: &glow::Context, program: glow::Program, name: &str, unit: u32, tex: Option<glow::Texture>) {
    // SAFETY: the context is current and `program` is in use.
    unsafe {
        gl.active_texture(glow::TEXTURE0 + unit);
        gl.bind_texture(glow::TEXTURE_2D, tex);
    }
    set_i(gl, program, name, unit as i32);
}

/// Colours the render thread draws with, read from the palette on the loop.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Colors {
    pub accent: [f32; 3],
    pub backdrop: [f32; 4],
}
