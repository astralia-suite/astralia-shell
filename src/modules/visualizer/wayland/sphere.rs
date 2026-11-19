//! The sphere visualizer: splatted noise particles, a shading pass, a glow pass, then a centred present.

use glow::HasContext;

use super::shaders::{
    Colors, FULLSCREEN_VS, PRESENT_FS, PRESENT_VS, SPHERE1_SPLAT_FS, bind_sampler, draw_quad, f32_bytes, glow_fs, program, quad_vbo, set_f, set_f2,
    set_f3, set_i, sphere1_vs, sphere2_fs,
};
use crate::config::settings::VisualizerParams;
use crate::config::visualizer_config::{CANVAS_FRACTION, CANVAS_MIN};

pub fn canvas_size(width: i32, height: i32) -> i32 {
    ((width.min(height) as f32 * CANVAS_FRACTION) as i32).max(CANVAS_MIN)
}

pub fn particle_thin_hash(px: f32, py: f32) -> f32 {
    // The literal is part of the reference hash.
    #[allow(clippy::approx_constant)]
    const TWO_PI: f32 = 6.283_185_3;
    let d = px * 127.1 + py * 311.7;
    let m = d % TWO_PI;
    let s = m.sin() * 43758.547;
    s - s.floor()
}

/// Texel-centre points of the canvas grid, row by row, minus those the hash thins out.
pub fn particle_points(canvas: i32, particle_thin: f32) -> Vec<f32> {
    let mut points = Vec::with_capacity((canvas.max(0) as usize).pow(2) * 2);
    for py in 0..canvas {
        for px in 0..canvas {
            if particle_thin_hash(px as f32, py as f32) < particle_thin {
                continue;
            }
            points.extend([px as f32 + 0.5, py as f32 + 0.5]);
        }
    }
    points
}

struct Programs {
    sphere1: glow::Program,
    sphere2: glow::Program,
    glow: glow::Program,
    present: glow::Program,
}

/// Two shading targets and the glow target, all canvas-sized.
#[derive(Default)]
struct Targets {
    fbo: [Option<glow::Framebuffer>; 3],
    tex: [Option<glow::Texture>; 3],
    canvas: i32,
}

pub struct Sphere {
    programs: Option<Programs>,
    targets: Targets,
    vbo: Option<glow::Buffer>,
    particle_vbo: Option<glow::Buffer>,
    particle_count: i32,
    grid: (i32, f32),
}

impl Default for Sphere {
    fn default() -> Self {
        Sphere { programs: None, targets: Targets::default(), vbo: None, particle_vbo: None, particle_count: 0, grid: (-1, -1.0) }
    }
}

impl Sphere {
    pub fn init(&mut self, gl: &glow::Context) -> bool {
        if self.programs.is_some() {
            return true;
        }
        let compiled = (
            program(gl, &sphere1_vs(), SPHERE1_SPLAT_FS, "visualizer_sphere1"),
            program(gl, FULLSCREEN_VS, &sphere2_fs(), "visualizer_sphere2"),
            program(gl, FULLSCREEN_VS, &glow_fs(), "visualizer_glow"),
            program(gl, PRESENT_VS, PRESENT_FS, "visualizer_present"),
        );
        let (Some(sphere1), Some(sphere2), Some(glow), Some(present)) = compiled else {
            ::log::error!("visualizer_sphere: shader compile failed");
            for p in [compiled.0, compiled.1, compiled.2, compiled.3].into_iter().flatten() {
                // SAFETY: the context is current.
                unsafe { gl.delete_program(p) };
            }
            return false;
        };
        self.programs = Some(Programs { sphere1, sphere2, glow, present });
        self.vbo = quad_vbo(gl);
        // SAFETY: the context is current.
        self.particle_vbo = unsafe { gl.create_buffer().ok() };
        true
    }

    fn delete_targets(&mut self, gl: &glow::Context) {
        // SAFETY: the context is current; the targets were created on it.
        unsafe {
            for tex in self.targets.tex.iter_mut().filter_map(Option::take) {
                gl.delete_texture(tex);
            }
            for fbo in self.targets.fbo.iter_mut().filter_map(Option::take) {
                gl.delete_framebuffer(fbo);
            }
        }
    }

    pub fn destroy(&mut self, gl: &glow::Context) {
        self.delete_targets(gl);
        // SAFETY: the context is current; every object was created on it.
        unsafe {
            if let Some(p) = self.programs.take() {
                for program in [p.sphere1, p.sphere2, p.glow, p.present] {
                    gl.delete_program(program);
                }
            }
            for vbo in [self.vbo.take(), self.particle_vbo.take()].into_iter().flatten() {
                gl.delete_buffer(vbo);
            }
        }
        *self = Sphere::default();
    }

    fn ensure_targets(&mut self, gl: &glow::Context, canvas: i32) {
        if self.targets.tex[0].is_some() && canvas == self.targets.canvas {
            return;
        }
        self.delete_targets(gl);
        self.targets.canvas = canvas;
        // SAFETY: the context is current.
        unsafe {
            for i in 0..3 {
                let (internal, ty) = if i == 0 { (glow::RGBA32F as i32, glow::FLOAT) } else { (glow::RGBA as i32, glow::UNSIGNED_BYTE) };
                let tex = gl.create_texture().ok();
                gl.bind_texture(glow::TEXTURE_2D, tex);
                gl.tex_image_2d(glow::TEXTURE_2D, 0, internal, canvas, canvas, 0, glow::RGBA, ty, glow::PixelUnpackData::Slice(None));
                for (pname, value) in [
                    (glow::TEXTURE_MIN_FILTER, glow::LINEAR),
                    (glow::TEXTURE_MAG_FILTER, glow::LINEAR),
                    (glow::TEXTURE_WRAP_S, glow::CLAMP_TO_EDGE),
                    (glow::TEXTURE_WRAP_T, glow::CLAMP_TO_EDGE),
                ] {
                    gl.tex_parameter_i32(glow::TEXTURE_2D, pname, value as i32);
                }
                let fbo = gl.create_framebuffer().ok();
                gl.bind_framebuffer(glow::FRAMEBUFFER, fbo);
                gl.framebuffer_texture_2d(glow::FRAMEBUFFER, glow::COLOR_ATTACHMENT0, glow::TEXTURE_2D, tex, 0);
                self.targets.tex[i] = tex;
                self.targets.fbo[i] = fbo;
            }
            gl.bind_texture(glow::TEXTURE_2D, None);
            gl.bind_framebuffer(glow::FRAMEBUFFER, None);
        }
    }

    fn ensure_particle_grid(&mut self, gl: &glow::Context, canvas: i32, particle_thin: f32) {
        if self.grid == (canvas, particle_thin) {
            return;
        }
        self.grid = (canvas, particle_thin);
        let points = particle_points(canvas, particle_thin);
        self.particle_count = (points.len() / 2) as i32;
        // SAFETY: the context is current.
        unsafe {
            gl.bind_buffer(glow::ARRAY_BUFFER, self.particle_vbo);
            gl.buffer_data_u8_slice(glow::ARRAY_BUFFER, f32_bytes(&points), glow::STATIC_DRAW);
            gl.bind_buffer(glow::ARRAY_BUFFER, None);
        }
    }

    #[allow(clippy::too_many_arguments)]
    fn set_audio_uniforms(
        gl: &glow::Context,
        prog: glow::Program,
        audio: (Option<glow::Texture>, Option<glow::Texture>),
        audio_size: i32,
        tick: i32,
        canvas: i32,
        params: &VisualizerParams,
        colors: &Colors,
    ) {
        set_f2(gl, prog, "resolution", canvas as f32, canvas as f32);
        set_f(gl, prog, "time", tick as f32);
        set_i(gl, prog, "audioRSize", audio_size);
        set_i(gl, prog, "audioLSize", audio_size);
        set_f3(gl, prog, "u_accent", colors.accent);
        set_i(gl, prog, "u_particleSize", params.particle_size);
        set_i(gl, prog, "u_complexity", params.fractal_complexity);
        set_f(gl, prog, "u_glowDirections", params.glow_directions);
        set_f(gl, prog, "u_glowQuality", params.glow_quality);
        bind_sampler(gl, prog, "audioR", 1, audio.1);
        bind_sampler(gl, prog, "audioL", 2, audio.0);
        // SAFETY: the context is current.
        unsafe { gl.active_texture(glow::TEXTURE0) };
    }

    #[allow(clippy::too_many_arguments)]
    pub fn render(
        &mut self,
        gl: &glow::Context,
        width: i32,
        height: i32,
        tick: i32,
        audio_l: Option<glow::Texture>,
        audio_r: Option<glow::Texture>,
        audio_size: i32,
        params: &VisualizerParams,
        colors: &Colors,
    ) {
        if self.programs.is_none() || width <= 0 || height <= 0 {
            return;
        }
        let canvas = canvas_size(width, height);
        self.ensure_targets(gl, canvas);
        self.ensure_particle_grid(gl, canvas, params.particle_thin);
        let (Some(p), Some(vbo)) = (&self.programs, self.vbo) else { return };
        let audio = (audio_l, audio_r);
        let [fbo0, fbo1, glow_fbo] = self.targets.fbo;
        let [tex0, tex1, glow_tex] = self.targets.tex;
        // SAFETY: the context is current; every bound object belongs to it.
        unsafe {
            gl.disable(glow::SCISSOR_TEST);
            gl.disable(glow::BLEND);
            for fbo in [fbo0, fbo1, glow_fbo] {
                gl.bind_framebuffer(glow::FRAMEBUFFER, fbo);
                gl.viewport(0, 0, canvas, canvas);
                gl.clear_color(0.0, 0.0, 0.0, 0.0);
                gl.clear(glow::COLOR_BUFFER_BIT);
            }
            gl.bind_framebuffer(glow::FRAMEBUFFER, fbo0);
            gl.use_program(Some(p.sphere1));
        }
        Self::set_audio_uniforms(gl, p.sphere1, audio, audio_size, tick, canvas, params, colors);
        // SAFETY: as above.
        unsafe {
            gl.enable(glow::BLEND);
            gl.blend_func(glow::ONE, glow::ONE);
            gl.bind_buffer(glow::ARRAY_BUFFER, self.particle_vbo);
            gl.vertex_attrib_pointer_f32(0, 2, glow::FLOAT, false, 0, 0);
            gl.enable_vertex_attrib_array(0);
            gl.draw_arrays(glow::POINTS, 0, self.particle_count);
            gl.disable_vertex_attrib_array(0);
            gl.disable(glow::BLEND);

            gl.bind_framebuffer(glow::FRAMEBUFFER, fbo1);
            gl.use_program(Some(p.sphere2));
        }
        Self::set_audio_uniforms(gl, p.sphere2, audio, audio_size, tick, canvas, params, colors);
        bind_sampler(gl, p.sphere2, "tex", 0, tex0);
        draw_quad(gl, vbo);

        // SAFETY: as above.
        unsafe {
            gl.bind_framebuffer(glow::FRAMEBUFFER, glow_fbo);
            gl.use_program(Some(p.glow));
        }
        Self::set_audio_uniforms(gl, p.glow, audio, audio_size, tick, canvas, params, colors);
        bind_sampler(gl, p.glow, "tex", 0, tex1);
        draw_quad(gl, vbo);

        let (off_x, off_y) = ((width - canvas) / 2, (height - canvas) / 2);
        let [r, g, b, a] = colors.backdrop;
        // SAFETY: as above.
        unsafe {
            gl.bind_framebuffer(glow::FRAMEBUFFER, None);
            gl.viewport(0, 0, width, height);
            gl.clear_color(r, g, b, a);
            gl.clear(glow::COLOR_BUFFER_BIT);
            gl.viewport(off_x, off_y, canvas, canvas);
            gl.enable(glow::BLEND);
            gl.blend_func(glow::ONE, glow::ONE_MINUS_SRC_ALPHA);
            gl.use_program(Some(p.present));
        }
        bind_sampler(gl, p.present, "tex", 0, glow_tex);
        draw_quad(gl, vbo);
        // SAFETY: as above.
        unsafe {
            gl.disable(glow::BLEND);
            gl.bind_framebuffer(glow::FRAMEBUFFER, None);
            gl.bind_texture(glow::TEXTURE_2D, None);
            gl.use_program(None);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn particle_grid() {
        assert_eq!(particle_thin_hash(0.0, 0.0), 0.0);
        assert_eq!(particle_thin_hash(3.0, 7.0), 507.0 / 1024.0);
        assert_eq!(particle_thin_hash(120.0, 45.0), 0.4765625);
        assert_eq!(particle_thin_hash(511.0, 2.0), 293.0 / 2048.0);
        assert_eq!(particle_points(4, 0.0).len(), 4 * 4 * 2, "a thin of 0 keeps every point");
        let thinned = particle_points(64, 0.5).len() / 2;
        assert!(thinned > 0 && thinned < 64 * 64, "a thin drops part of the grid");
        assert_eq!(&particle_points(2, 0.0)[..4], &[0.5, 0.5, 1.5, 0.5], "points sit at texel centres, row by row");
    }

    #[test]
    fn sphere_canvas_size() {
        assert_eq!(canvas_size(1000, 800), 560, "the smaller side times the fraction");
        assert_eq!(canvas_size(100, 2000), 200, "never below the minimum");
    }
}
