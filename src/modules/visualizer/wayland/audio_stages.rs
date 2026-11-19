//! The GL audio pipeline: max-hold, gravity, a five-frame average and the smoothing pass, per channel.

use glow::HasContext;

use super::shaders::{
    AUDIO_AVERAGE_FS, AUDIO_GRAVITY_FS, AUDIO_PASS_FS, AUDIO_SMOOTH_FS, FULLSCREEN_VS, bind_sampler, draw_quad, program, quad_vbo, set_f, set_i,
};
use crate::config::visualizer_config::{
    ADJACENT_SAMPLE_NUMS, FPS, GRAVITY_AVERAGE_FRAMES, GRAVITY_STEP, SAMPLE_HYBRID_WEIGHT, SAMPLE_MODE, SAMPLE_RANGE, SAMPLE_SCALE, SMOOTH_FACTOR,
};

const RING: usize = 5;
const R16: i32 = glow::R16 as i32;

/// One framebuffer and a texture per channel (0 right, 1 left).
#[derive(Default)]
struct Target {
    fbo: Option<glow::Framebuffer>,
    tex: [Option<glow::Texture>; 2],
}

struct Programs {
    pass: glow::Program,
    gravity: glow::Program,
    average: glow::Program,
    smooth: glow::Program,
}

pub struct AudioStages {
    programs: Option<Programs>,
    vbo: Option<glow::Buffer>,
    raw: [Option<glow::Texture>; 2],
    pass: Target,
    average: Target,
    smooth: Target,
    ring: [Target; RING],
    out_idx: [usize; 2],
    quantized: Vec<u16>,
    size: i32,
    fps: i32,
}

impl Default for AudioStages {
    fn default() -> Self {
        AudioStages {
            programs: None,
            vbo: None,
            raw: [None; 2],
            pass: Target::default(),
            average: Target::default(),
            smooth: Target::default(),
            ring: Default::default(),
            out_idx: [0; 2],
            quantized: Vec::new(),
            size: 0,
            fps: FPS,
        }
    }
}

fn u16_bytes(v: &[u16]) -> &[u8] {
    // SAFETY: any u16 slice is a valid byte slice of twice its length.
    unsafe { std::slice::from_raw_parts(v.as_ptr().cast(), std::mem::size_of_val(v)) }
}

fn r16_texture(gl: &glow::Context, size: i32) -> Option<glow::Texture> {
    let zeros = vec![0u16; size.max(0) as usize];
    // SAFETY: the context is current; `zeros` holds `size` R16 texels.
    unsafe {
        let tex = gl.create_texture().ok()?;
        gl.bind_texture(glow::TEXTURE_2D, Some(tex));
        for (pname, value) in [
            (glow::TEXTURE_MIN_FILTER, glow::NEAREST),
            (glow::TEXTURE_MAG_FILTER, glow::NEAREST),
            (glow::TEXTURE_WRAP_S, glow::CLAMP_TO_EDGE),
            (glow::TEXTURE_WRAP_T, glow::CLAMP_TO_EDGE),
        ] {
            gl.tex_parameter_i32(glow::TEXTURE_2D, pname, value as i32);
        }
        gl.tex_image_2d(glow::TEXTURE_2D, 0, R16, size, 1, 0, glow::RED, glow::UNSIGNED_SHORT, glow::PixelUnpackData::Slice(Some(u16_bytes(&zeros))));
        gl.bind_texture(glow::TEXTURE_2D, None);
        Some(tex)
    }
}

/// An R16 framebuffer can be rendered to.
fn r16_renderable(gl: &glow::Context) -> Result<(), u32> {
    // SAFETY: the context is current; the probe objects are deleted before returning.
    unsafe {
        let tex = gl.create_texture().map_err(|_| 0u32)?;
        gl.bind_texture(glow::TEXTURE_2D, Some(tex));
        gl.tex_image_2d(glow::TEXTURE_2D, 0, R16, 4, 1, 0, glow::RED, glow::UNSIGNED_SHORT, glow::PixelUnpackData::Slice(None));
        let fbo = gl.create_framebuffer().map_err(|_| 0u32)?;
        gl.bind_framebuffer(glow::FRAMEBUFFER, Some(fbo));
        gl.framebuffer_texture_2d(glow::FRAMEBUFFER, glow::COLOR_ATTACHMENT0, glow::TEXTURE_2D, Some(tex), 0);
        let status = gl.check_framebuffer_status(glow::FRAMEBUFFER);
        gl.bind_framebuffer(glow::FRAMEBUFFER, None);
        gl.delete_framebuffer(fbo);
        gl.delete_texture(tex);
        if status == glow::FRAMEBUFFER_COMPLETE { Ok(()) } else { Err(status) }
    }
}

impl AudioStages {
    pub fn init(&mut self, gl: &glow::Context) -> bool {
        if self.programs.is_some() {
            return true;
        }
        let compiled = (
            program(gl, FULLSCREEN_VS, AUDIO_PASS_FS, "visualizer_audio_pass"),
            program(gl, FULLSCREEN_VS, AUDIO_GRAVITY_FS, "visualizer_audio_gravity"),
            program(gl, FULLSCREEN_VS, AUDIO_AVERAGE_FS, "visualizer_audio_average"),
            program(gl, FULLSCREEN_VS, AUDIO_SMOOTH_FS, "visualizer_audio_smooth"),
        );
        let (Some(pass), Some(gravity), Some(average), Some(smooth)) = compiled else {
            ::log::error!("visualizer_audio_stages: shader compile failed");
            for p in [compiled.0, compiled.1, compiled.2, compiled.3].into_iter().flatten() {
                // SAFETY: the context is current.
                unsafe { gl.delete_program(p) };
            }
            return false;
        };
        let programs = Programs { pass, gravity, average, smooth };
        self.vbo = quad_vbo(gl);
        let has_norm16 = gl.supported_extensions().contains("GL_EXT_texture_norm16");
        if !has_norm16 {
            ::log::error!("visualizer_audio_stages: GL_EXT_texture_norm16 unsupported");
            self.programs = Some(programs);
            self.destroy(gl);
            return false;
        }
        if let Err(status) = r16_renderable(gl) {
            ::log::error!("visualizer_audio_stages: R16 render target unsupported (0x{status:x})");
            self.programs = Some(programs);
            self.destroy(gl);
            return false;
        }
        // SAFETY: the context is current.
        unsafe { gl.use_program(Some(programs.smooth)) };
        set_i(gl, programs.smooth, "sample_mode", SAMPLE_MODE);
        set_f(gl, programs.smooth, "sample_hybrid_weight", SAMPLE_HYBRID_WEIGHT);
        set_f(gl, programs.smooth, "sample_scale", SAMPLE_SCALE);
        set_f(gl, programs.smooth, "sample_range", SAMPLE_RANGE);
        set_f(gl, programs.smooth, "smooth_factor", SMOOTH_FACTOR);
        // SAFETY: the context is current.
        unsafe { gl.use_program(None) };
        self.programs = Some(programs);
        true
    }

    pub fn destroy(&mut self, gl: &glow::Context) {
        // SAFETY: the context is current; every object was created on it.
        unsafe {
            if let Some(p) = self.programs.take() {
                for program in [p.pass, p.gravity, p.average, p.smooth] {
                    gl.delete_program(program);
                }
            }
            let targets = [&mut self.pass, &mut self.average, &mut self.smooth].into_iter().chain(self.ring.iter_mut());
            for target in targets {
                for tex in target.tex.iter_mut().filter_map(Option::take) {
                    gl.delete_texture(tex);
                }
                if let Some(fbo) = target.fbo.take() {
                    gl.delete_framebuffer(fbo);
                }
            }
            for tex in self.raw.iter_mut().filter_map(Option::take) {
                gl.delete_texture(tex);
            }
            if let Some(vbo) = self.vbo.take() {
                gl.delete_buffer(vbo);
            }
        }
        *self = AudioStages::default();
    }

    pub fn smooth_l(&self) -> Option<glow::Texture> {
        self.smooth.tex[1]
    }

    pub fn smooth_r(&self) -> Option<glow::Texture> {
        self.smooth.tex[0]
    }

    pub fn size(&self) -> i32 {
        self.size
    }

    pub fn ready(&self) -> bool {
        self.programs.is_some() && self.size > 0
    }

    fn upload_raw(&mut self, gl: &glow::Context, channel: usize, data: &[f32], size: i32) {
        self.quantized.clear();
        self.quantized.extend(data[..size as usize].iter().map(|v| (v.clamp(0.0, 1.0) * 65535.0 + 0.5) as u16));
        // SAFETY: the context is current; `quantized` holds `size` R16 texels.
        unsafe {
            gl.bind_texture(glow::TEXTURE_2D, self.raw[channel]);
            gl.tex_image_2d(glow::TEXTURE_2D, 0, R16, size, 1, 0, glow::RED, glow::UNSIGNED_SHORT, glow::PixelUnpackData::Slice(Some(u16_bytes(&self.quantized))));
            gl.bind_texture(glow::TEXTURE_2D, None);
        }
    }

    pub fn run(&mut self, gl: &glow::Context, size: i32, l: &[f32], r: &[f32], fps: i32) -> bool {
        if self.programs.is_none() || size <= 0 || (l.len() as i32) < size || (r.len() as i32) < size {
            return false;
        }
        self.size = size;
        self.fps = fps;
        if self.raw[0].is_none() {
            self.raw = [r16_texture(gl, size), r16_texture(gl, size)];
        }
        self.upload_raw(gl, 0, r, size);
        self.upload_raw(gl, 1, l, size);
        // SAFETY: the context is current.
        unsafe { gl.disable(glow::SCISSOR_TEST) };
        self.run_channel(gl, 0, size);
        self.run_channel(gl, 1, size);
        // SAFETY: the context is current.
        unsafe {
            gl.bind_framebuffer(glow::FRAMEBUFFER, None);
            gl.disable(glow::BLEND);
        }
        true
    }

    fn run_channel(&mut self, gl: &glow::Context, c: usize, size: i32) {
        let Some(p) = &self.programs else { return };
        let (pass, gravity, average, smooth) = (p.pass, p.gravity, p.average, p.smooth);
        let Some(vbo) = self.vbo else { return };
        // The right channel samples on unit 1, the left on unit 2.
        let offset = c as u32 + 1;
        let oidx = self.out_idx[c];

        bind_target(gl, &mut self.pass, c, size);
        let pass_tex = self.pass.tex[c];
        // SAFETY: the context is current; every bound object belongs to it.
        unsafe {
            gl.viewport(0, 0, size, 1);
            gl.use_program(Some(pass));
        }
        bind_sampler(gl, pass, "audioR", offset, self.raw[c]);
        set_f(gl, pass, "u_texWidth", size as f32);
        // SAFETY: as above.
        unsafe {
            gl.enable(glow::BLEND);
            gl.blend_equation(glow::MAX);
        }
        draw_quad(gl, vbo);
        // SAFETY: as above.
        unsafe {
            gl.blend_equation(glow::FUNC_ADD);
            gl.disable(glow::BLEND);
            gl.finish();
            gl.use_program(Some(gravity));
        }
        bind_sampler(gl, gravity, "audioR", offset, pass_tex);
        set_f(gl, gravity, "diff", GRAVITY_STEP / self.fps as f32);
        set_f(gl, gravity, "u_texWidth", size as f32);
        // SAFETY: as above.
        unsafe { gl.viewport(0, 0, size, 1) };
        draw_quad(gl, vbo);

        bind_target(gl, &mut self.ring[oidx], c, size);
        // SAFETY: as above.
        unsafe {
            gl.viewport(0, 0, size, 1);
            gl.use_program(Some(pass));
        }
        bind_sampler(gl, pass, "audioR", offset, pass_tex);
        set_f(gl, pass, "u_texWidth", size as f32);
        draw_quad(gl, vbo);

        bind_target(gl, &mut self.average, c, size);
        // SAFETY: as above.
        unsafe {
            gl.viewport(0, 0, size, 1);
            gl.use_program(Some(average));
        }
        set_i(gl, average, "avgFrames", GRAVITY_AVERAGE_FRAMES);
        set_f(gl, average, "u_texWidth", size as f32);
        for t in 0..RING {
            let frame = (oidx + RING - t) % RING;
            bind_sampler(gl, average, &format!("audioR{t}"), offset + 1 + t as u32, self.ring[frame].tex[c]);
        }
        draw_quad(gl, vbo);
        self.out_idx[c] = (oidx + 1) % RING;

        bind_target(gl, &mut self.smooth, c, size);
        // SAFETY: as above.
        unsafe {
            gl.viewport(0, 0, size, 1);
            gl.use_program(Some(smooth));
        }
        set_i(gl, smooth, "audioRSize", size);
        set_i(gl, smooth, "adjacentSampleNums", ADJACENT_SAMPLE_NUMS);
        bind_sampler(gl, smooth, "audioR", offset, self.average.tex[c]);
        draw_quad(gl, vbo);
        // SAFETY: as above.
        unsafe {
            gl.active_texture(glow::TEXTURE0);
            gl.bind_texture(glow::TEXTURE_2D, None);
            gl.use_program(None);
        }
    }
}

fn bind_target(gl: &glow::Context, target: &mut Target, c: usize, size: i32) {
    // SAFETY: the context is current.
    unsafe {
        if target.fbo.is_none() {
            target.fbo = gl.create_framebuffer().ok();
        }
        if target.tex[c].is_none() {
            target.tex[c] = r16_texture(gl, size);
        }
        gl.bind_framebuffer(glow::FRAMEBUFFER, target.fbo);
        gl.framebuffer_texture_2d(glow::FRAMEBUFFER, glow::COLOR_ATTACHMENT0, glow::TEXTURE_2D, target.tex[c], 0);
    }
}

#[cfg(test)]
mod tests {
    use super::AudioStages;
    use crate::backend::wayland::render::egl::Egl;
    use crate::modules::visualizer::cpu_stages::CpuStages;
    use glow::HasContext;

    const SIZE: i32 = 512;

    fn exact(v: f32) -> f32 {
        (v * 65535.0).round() / 65535.0
    }

    fn read_smooth(gl: &glow::Context, tex: glow::Texture) -> Vec<f32> {
        let mut out = vec![0u16; SIZE as usize * 4];
        // SAFETY: the context is current; `out` holds SIZE RGBA16 texels.
        unsafe {
            let fbo = gl.create_framebuffer().unwrap();
            gl.bind_framebuffer(glow::FRAMEBUFFER, Some(fbo));
            gl.framebuffer_texture_2d(glow::FRAMEBUFFER, glow::COLOR_ATTACHMENT0, glow::TEXTURE_2D, Some(tex), 0);
            let bytes = std::slice::from_raw_parts_mut(out.as_mut_ptr().cast::<u8>(), out.len() * 2);
            gl.read_pixels(0, 0, SIZE, 1, glow::RGBA, glow::UNSIGNED_SHORT, glow::PixelPackData::Slice(Some(bytes)));
            gl.bind_framebuffer(glow::FRAMEBUFFER, None);
            gl.delete_framebuffer(fbo);
        }
        out.chunks(4).map(|t| f32::from(t[0]) / 65535.0).collect()
    }

    #[test]
    fn audio_stages_match_cpu() {
        let egl = match Egl::surfaceless() {
            Ok(egl) => egl,
            Err(e) => return println!("skipped: {e}"),
        };
        assert!(egl.make_current(None));
        let gl = egl.gl();
        let mut stages = AudioStages::default();
        if !stages.init(&gl) {
            return println!("skipped: GL_EXT_texture_norm16 or an R16 render target is missing");
        }
        let loud: Vec<f32> = (0..SIZE).map(|i| exact(0.85 * ((i as f32) * 0.07).sin().abs())).collect();
        let quiet: Vec<f32> = (0..SIZE).map(|i| exact(0.1 * ((i as f32) * 0.3).cos().abs())).collect();
        let mut cpu = CpuStages::default();
        let mut peak = 0.0f32;
        for frame in 0..6 {
            let (l, r) = if frame < 3 { (&loud, &quiet) } else { (&quiet, &loud) };
            assert!(stages.run(&gl, SIZE, l, r, 60));
            assert!(cpu.run(SIZE, l, r, 60));
            let got = read_smooth(&gl, stages.smooth_l().unwrap());
            peak = got.iter().fold(peak, |m, &g| m.max(g));
            for (i, &g) in got.iter().enumerate() {
                let want = cpu.smooth_texel(0, i as i32);
                assert!((g - want).abs() <= 4.0 / 65535.0, "frame {frame} texel {i}: gl {g}, cpu {want}");
            }
        }
        assert!(peak > 0.05, "the pipeline produced a spectrum, peak {peak}");
        stages.destroy(&gl);
    }
}
