//! Lightning: a bolt between two points, a circular shock wave and a rounded-rectangle shock wave.

use glow::HasContext;

use super::renderer::Renderer;

const BURST_FRAG: &str = include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/assets/shaders/thunder/thunder_burst.frag"));
const SHOCK_FRAG: &str = include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/assets/shaders/logout/thunder_shock.frag"));
const RECT_FRAG: &str = include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/assets/shaders/launcher/thunder_rect_shock.frag"));

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ThunderParams {
    pub ax: f32,
    pub ay: f32,
    pub bx: f32,
    pub by: f32,
    pub time_s: f32,
    pub progress: f32,
    pub intensity: f32,
    pub seed: f32,
    pub amp: f32,
    pub thick: f32,
    pub pad: f32,
    pub stitch: bool,
    pub core: [f32; 4],
    pub glow: [f32; 4],
}

impl Default for ThunderParams {
    fn default() -> Self {
        ThunderParams {
            ax: 0.0,
            ay: 0.0,
            bx: 0.0,
            by: 0.0,
            time_s: 0.0,
            progress: 1.0,
            intensity: 1.0,
            seed: 0.0,
            amp: 14.0,
            thick: 1.0,
            pad: 42.0,
            stitch: false,
            core: [0.0; 4],
            glow: [0.0; 4],
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ThunderShockParams {
    pub cx: f32,
    pub cy: f32,
    pub radius: f32,
    pub time_s: f32,
    pub progress: f32,
    pub intensity: f32,
    pub core: [f32; 4],
    pub glow: [f32; 4],
}

impl Default for ThunderShockParams {
    fn default() -> Self {
        ThunderShockParams { cx: 0.0, cy: 0.0, radius: 0.0, time_s: 0.0, progress: 0.0, intensity: 1.0, core: [0.0; 4], glow: [0.0; 4] }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ThunderRectShockParams {
    pub cx: f32,
    pub cy: f32,
    pub half_w: f32,
    pub half_h: f32,
    pub corner: f32,
    pub reach: f32,
    pub time_s: f32,
    pub progress: f32,
    pub intensity: f32,
    pub core: [f32; 4],
    pub glow: [f32; 4],
}

impl Default for ThunderRectShockParams {
    fn default() -> Self {
        ThunderRectShockParams {
            cx: 0.0,
            cy: 0.0,
            half_w: 0.0,
            half_h: 0.0,
            corner: 0.0,
            reach: 0.0,
            time_s: 0.0,
            progress: 0.0,
            intensity: 1.0,
            core: [0.0; 4],
            glow: [0.0; 4],
        }
    }
}

/// A program built on first use; a failed build (logged by the compiler) is not retried.
type Lazy = Option<Option<glow::Program>>;

fn program(slot: &mut Lazy, renderer: &Renderer, source: &str, label: &str) -> Option<glow::Program> {
    *slot.get_or_insert_with(|| renderer.compile_program(source, label).ok())
}

/// The three lightning programs, built lazily on the shared context.
#[derive(Default)]
pub struct ThunderBurst {
    bolt: Lazy,
    shock: Lazy,
    rect: Lazy,
}

fn set2(gl: &glow::Context, p: glow::Program, name: &str, x: f32, y: f32) {
    // SAFETY: the context is current and `p` is in use.
    unsafe { gl.uniform_2_f32(gl.get_uniform_location(p, name).as_ref(), x, y) };
}

fn set1(gl: &glow::Context, p: glow::Program, name: &str, v: f32) {
    // SAFETY: the context is current and `p` is in use.
    unsafe { gl.uniform_1_f32(gl.get_uniform_location(p, name).as_ref(), v) };
}

fn set4(gl: &glow::Context, p: glow::Program, name: &str, v: &[f32; 4]) {
    // SAFETY: the context is current and `p` is in use.
    unsafe { gl.uniform_4_f32_slice(gl.get_uniform_location(p, name).as_ref(), v) };
}

impl ThunderBurst {
    /// A bolt from `a` to `b`, drawn on a quad padded for its wander and glow.
    pub fn burst_draw(&mut self, renderer: &Renderer, p: &ThunderParams) {
        let Some(prog) = program(&mut self.bolt, renderer, BURST_FRAG, "thunder_bolt") else { return };
        let pad = p.pad + p.amp * 3.0 + p.thick * 30.0;
        let (min_x, min_y) = (p.ax.min(p.bx) - pad, p.ay.min(p.by) - pad);
        let (w, h) = ((p.bx - p.ax).abs() + pad * 2.0, (p.by - p.ay).abs() + pad * 2.0);
        renderer.draw_custom(prog, min_x, min_y, w, h, |gl, prog| {
            set2(gl, prog, "u_size", w, h);
            set2(gl, prog, "u_a", p.ax - min_x, p.ay - min_y);
            set2(gl, prog, "u_b", p.bx - min_x, p.by - min_y);
            set1(gl, prog, "u_time", p.time_s);
            set1(gl, prog, "u_progress", p.progress);
            set1(gl, prog, "u_intensity", p.intensity);
            set1(gl, prog, "u_seed", p.seed);
            set1(gl, prog, "u_amp", p.amp);
            set1(gl, prog, "u_thick", p.thick);
            set1(gl, prog, "u_stitch", if p.stitch { 1.0 } else { 0.0 });
            set4(gl, prog, "u_core", &p.core);
            set4(gl, prog, "u_glow", &p.glow);
        });
    }

    /// A ring growing from `c` to `radius` as `progress` runs; nothing without a radius.
    pub fn shock_draw(&mut self, renderer: &Renderer, p: &ThunderShockParams) {
        if p.radius <= 0.0 {
            return;
        }
        let Some(prog) = program(&mut self.shock, renderer, SHOCK_FRAG, "thunder_shock") else { return };
        let (min_x, min_y, side) = (p.cx - p.radius, p.cy - p.radius, p.radius * 2.0);
        renderer.draw_custom(prog, min_x, min_y, side, side, |gl, prog| {
            set2(gl, prog, "u_size", side, side);
            set2(gl, prog, "u_center", p.cx - min_x, p.cy - min_y);
            set1(gl, prog, "u_time", p.time_s);
            set1(gl, prog, "u_progress", p.progress);
            set1(gl, prog, "u_radius", p.radius);
            set1(gl, prog, "u_intensity", p.intensity);
            set4(gl, prog, "u_core", &p.core);
            set4(gl, prog, "u_glow", &p.glow);
        });
    }

    /// A rounded rectangle's outline pushed out by `reach` as `progress` runs.
    pub fn rect_shock_draw(&mut self, renderer: &Renderer, p: &ThunderRectShockParams) {
        if p.half_w <= 0.0 || p.half_h <= 0.0 {
            return;
        }
        let Some(prog) = program(&mut self.rect, renderer, RECT_FRAG, "thunder_rect_shock") else { return };
        let pad = p.reach + 120.0;
        let (min_x, min_y) = (p.cx - p.half_w - pad, p.cy - p.half_h - pad);
        let (w, h) = ((p.half_w + pad) * 2.0, (p.half_h + pad) * 2.0);
        renderer.draw_custom(prog, min_x, min_y, w, h, |gl, prog| {
            set2(gl, prog, "u_size", w, h);
            set2(gl, prog, "u_center", p.cx - min_x, p.cy - min_y);
            set2(gl, prog, "u_half", p.half_w, p.half_h);
            set1(gl, prog, "u_corner", p.corner);
            set1(gl, prog, "u_time", p.time_s);
            set1(gl, prog, "u_progress", p.progress);
            set1(gl, prog, "u_reach", p.reach);
            set1(gl, prog, "u_intensity", p.intensity);
            set4(gl, prog, "u_core", &p.core);
            set4(gl, prog, "u_glow", &p.glow);
        });
    }

    /// Deletes the built programs; call with the context current.
    pub fn destroy(&mut self, renderer: &Renderer) {
        for slot in [&mut self.bolt, &mut self.shock, &mut self.rect] {
            if let Some(Some(prog)) = slot.take() {
                renderer.delete_program(prog);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::backend::wayland::render::renderer::{offscreen, read_pixel};

    const W: i32 = 128;
    const H: i32 = 64;
    const WHITE: [f32; 4] = [1.0, 1.0, 1.0, 1.0];

    #[test]
    fn thunder_draws() {
        let Some((_egl, mut r)) = offscreen(W, H) else { return };
        let mut tb = ThunderBurst::default();
        let frame = |r: &mut Renderer| {
            r.begin_frame(W, H, 1);
            r.clear();
        };

        frame(&mut r);
        let bolt = ThunderParams { ax: 10.0, ay: 32.0, bx: 118.0, by: 32.0, amp: 0.0, pad: 0.0, thick: 1.0, core: WHITE, glow: WHITE, ..ThunderParams::default() };
        tb.burst_draw(&r, &bolt);
        let on = (40..=88).step_by(8).any(|x| read_pixel(&r, H, x, 32)[3] > 0);
        assert!(on, "a bolt paints along its segment");
        assert_eq!(read_pixel(&r, H, 0, 0)[3], 0, "and leaves a far corner clear");

        let shock = |progress, radius| ThunderShockParams { cx: 64.0, cy: 32.0, radius, progress, core: WHITE, glow: WHITE, ..ThunderShockParams::default() };
        frame(&mut r);
        tb.shock_draw(&r, &shock(1.0, 30.0));
        assert_eq!(read_pixel(&r, H, 64, 32)[3], 0, "a finished shock paints nothing");
        tb.shock_draw(&r, &shock(0.5, 0.0));
        assert_eq!(read_pixel(&r, H, 64, 32)[3], 0, "a shock with no radius paints nothing");
        tb.shock_draw(&r, &shock(0.5, 30.0));
        assert!(read_pixel(&r, H, 64, 32)[3] > 0, "a running shock paints");

        frame(&mut r);
        let rect = |progress| ThunderRectShockParams {
            cx: 64.0,
            cy: 32.0,
            half_w: 20.0,
            half_h: 10.0,
            corner: 4.0,
            reach: 20.0,
            progress,
            core: WHITE,
            glow: WHITE,
            ..ThunderRectShockParams::default()
        };
        tb.rect_shock_draw(&r, &rect(1.0));
        assert_eq!(read_pixel(&r, H, 64, 32)[3], 0, "a finished rectangle shock paints nothing");
        tb.destroy(&r);
    }
}
