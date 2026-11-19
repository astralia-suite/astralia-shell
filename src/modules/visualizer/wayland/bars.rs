//! The bar visualizer: one full-window pass over the smoothed spectrum.

use glow::HasContext;

use super::shaders::{BAR_FS, Colors, FULLSCREEN_VS, bind_sampler, draw_quad, program, quad_vbo, set_f, set_f2, set_f3, set_i};
use crate::config::visualizer_config::{BAR_HEIGHT_RATIO, BAR_MIN_HEIGHT, BAR_OPACITY, BAR_RADIUS, BAR_SPACING, BAR_WIDTH};
use crate::modules::visualizer::bars::bar_count;

#[derive(Default)]
pub struct Bars {
    program: Option<glow::Program>,
    vbo: Option<glow::Buffer>,
}

impl Bars {
    pub fn init(&mut self, gl: &glow::Context) -> bool {
        if self.program.is_some() {
            return true;
        }
        self.program = program(gl, FULLSCREEN_VS, BAR_FS, "visualizer_bar");
        if self.program.is_none() {
            ::log::error!("visualizer_bar: shader compile failed");
            return false;
        }
        self.vbo = quad_vbo(gl);
        true
    }

    pub fn destroy(&mut self, gl: &glow::Context) {
        // SAFETY: the context is current; both objects were created on it.
        unsafe {
            if let Some(p) = self.program.take() {
                gl.delete_program(p);
            }
            if let Some(vbo) = self.vbo.take() {
                gl.delete_buffer(vbo);
            }
        }
    }

    pub fn render(&self, gl: &glow::Context, width: i32, height: i32, audio_l: Option<glow::Texture>, audio_r: Option<glow::Texture>, colors: &Colors) {
        let (Some(prog), Some(vbo)) = (self.program, self.vbo) else { return };
        if width <= 0 || height <= 0 {
            return;
        }
        let [r, g, b, a] = colors.backdrop;
        // SAFETY: the context is current; every bound object belongs to it.
        unsafe {
            gl.bind_framebuffer(glow::FRAMEBUFFER, None);
            gl.viewport(0, 0, width, height);
            gl.clear_color(r, g, b, a);
            gl.clear(glow::COLOR_BUFFER_BIT);
            gl.enable(glow::BLEND);
            gl.blend_func(glow::ONE, glow::ONE_MINUS_SRC_ALPHA);
            gl.use_program(Some(prog));
        }
        set_f2(gl, prog, "u_resolution", width as f32, height as f32);
        set_f3(gl, prog, "u_accent", colors.accent);
        set_i(gl, prog, "u_barCount", bar_count(width as f32));
        set_f(gl, prog, "u_barWidth", BAR_WIDTH);
        set_f(gl, prog, "u_barSpacing", BAR_SPACING);
        set_f(gl, prog, "u_barRadius", BAR_RADIUS);
        set_f(gl, prog, "u_barHeightRatio", BAR_HEIGHT_RATIO);
        set_f(gl, prog, "u_barOpacity", BAR_OPACITY);
        set_f(gl, prog, "u_minBarHeight", BAR_MIN_HEIGHT);
        bind_sampler(gl, prog, "audioR", 1, audio_r);
        bind_sampler(gl, prog, "audioL", 2, audio_l);
        let band_h = (BAR_HEIGHT_RATIO * height as f32) as i32 + 1;
        // SAFETY: as above.
        unsafe {
            gl.active_texture(glow::TEXTURE0);
            gl.enable(glow::SCISSOR_TEST);
            gl.scissor(0, 0, width, band_h);
        }
        draw_quad(gl, vbo);
        // SAFETY: as above.
        unsafe {
            gl.disable(glow::SCISSOR_TEST);
            gl.disable(glow::BLEND);
            gl.use_program(None);
            gl.bind_texture(glow::TEXTURE_2D, None);
        }
    }
}
