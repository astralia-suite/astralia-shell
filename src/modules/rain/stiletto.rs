//! Comet rain: stiletto heads over fading trails.

use std::cell::OnceCell;

use pangocairo::cairo;

use super::random::{random01, random_range};
use super::{Frame, argb_buffer, decay, with_cairo};
use crate::config::rain_config::{
    ASYNC_SPEED_MAX, ASYNC_SPEED_MIN, RESET_CHANCE, STILETTO_COLUMN_SPACING_PX, STILETTO_HEAD_HEIGHT_PX, STILETTO_STEP_PX,
    STILETTO_TRAIL_WIDTH_PX,
};
use crate::render::decode::{asset_path, decode_svg};
use crate::render::theme::palette;
use crate::render::tokens::Color;

const START_DROP: f32 = -2.0 * STILETTO_STEP_PX;

fn load_sprite() -> Option<cairo::ImageSurface> {
    let h = STILETTO_HEAD_HEIGHT_PX as i32;
    let pixels = decode_svg(&asset_path("stiletto.svg"), h * 1000, h)
        .inspect_err(|e| ::log::warn!("rain: stiletto sprite: {e}"))
        .ok()?;
    let mut surface = cairo::ImageSurface::create(cairo::Format::ARgb32, pixels.width, pixels.height).ok()?;
    let stride = surface.stride() as usize;
    {
        let mut data = surface.data().ok()?;
        for (y, src) in pixels.data.chunks_exact(pixels.width as usize * 4).enumerate() {
            let row = &mut data[y * stride..][..pixels.width as usize * 4];
            for (dst, p) in row.as_chunks_mut::<4>().0.iter_mut().zip(src.as_chunks::<4>().0) {
                let argb = u32::from(p[3]) << 24 | u32::from(p[0]) << 16 | u32::from(p[1]) << 8 | u32::from(p[2]);
                dst.copy_from_slice(&argb.to_ne_bytes());
            }
        }
    }
    surface.mark_dirty();
    Some(surface)
}

thread_local! {
    static SPRITE: OnceCell<Option<cairo::ImageSurface>> = const { OnceCell::new() };
}

fn draw_sprite_centered(cr: &cairo::Context, sprite: &cairo::ImageSurface, cx: f32, y: f32, color: Color) {
    let tx = cx - sprite.width() as f32 / 2.0;
    let ty = y - sprite.height() as f32 / 2.0;
    cr.set_source_rgba(f64::from(color.r), f64::from(color.g), f64::from(color.b), f64::from(color.a));
    let _ = cr.mask_surface(sprite, f64::from(tx.round()), f64::from(ty.round()));
}

fn draw_trail_segment(cr: &cairo::Context, x: f32, y0: f32, y1: f32, a: Color) {
    cr.set_source_rgba(f64::from(a.r), f64::from(a.g), f64::from(a.b), f64::from(a.a));
    cr.set_line_width(f64::from(STILETTO_TRAIL_WIDTH_PX));
    cr.set_line_cap(cairo::LineCap::Round);
    cr.move_to(f64::from(x.round()), f64::from(y0));
    cr.line_to(f64::from(x.round()), f64::from(y1));
    let _ = cr.stroke();
}

#[derive(Clone, Copy)]
struct Comet {
    drop: f32,
    ever_reset: bool,
    last_drop: f32,
    last_valid: bool,
    speed: f32,
}

#[derive(Default)]
pub struct Stiletto {
    width: i32,
    height: i32,
    column_count: i32,
    origin_x: f32,
    sweeping: bool,
    sweep_drop: f32,
    buffer: Vec<u8>,
    frame: Vec<u8>,
    stride: i32,
    comets: Vec<Comet>,
}

impl Stiletto {
    pub fn rebuild(&mut self, width: i32, height: i32, async_speed: bool) {
        self.width = width.max(1);
        self.height = height.max(1);
        self.column_count = ((self.width as f32 / STILETTO_COLUMN_SPACING_PX) as i32).max(1);
        let content_w = (self.column_count - 1) as f32 * STILETTO_COLUMN_SPACING_PX;
        self.origin_x = (self.width as f32 - content_w) / 2.0;
        self.comets = (0..self.column_count)
            .map(|_| Comet {
                drop: START_DROP,
                ever_reset: false,
                last_drop: 0.0,
                last_valid: false,
                speed: if async_speed { random_range(ASYNC_SPEED_MIN, ASYNC_SPEED_MAX) } else { 1.0 },
            })
            .collect();
        self.sweeping = true;
        self.sweep_drop = START_DROP;
        (self.buffer, self.stride) = argb_buffer(self.width, self.height);
        self.frame = vec![0; self.buffer.len()];
    }

    pub fn tick(&mut self) {
        if self.buffer.is_empty() || self.comets.is_empty() {
            return;
        }
        decay(&mut self.buffer, self.width, self.height, self.stride);
        let accent = palette().accent;
        let pop_b = palette().pop_b;
        let (sweeping, sweep_drop, origin_x) = (self.sweeping, self.sweep_drop, self.origin_x);
        let head_of = |c: &Comet| if sweeping { sweep_drop } else { c.drop };
        let comets = &mut self.comets;
        with_cairo(&mut self.buffer, self.width, self.height, self.stride, |cr| {
            for (i, c) in comets.iter_mut().enumerate() {
                let x = origin_x + i as f32 * STILETTO_COLUMN_SPACING_PX;
                let head = head_of(c);
                if head >= 0.0 {
                    if c.last_valid {
                        draw_trail_segment(cr, x, c.last_drop, head, accent);
                    }
                    c.last_drop = head;
                    c.last_valid = true;
                } else {
                    c.last_valid = false;
                }
            }
        });

        self.frame.copy_from_slice(&self.buffer);
        let comets = &self.comets;
        SPRITE.with(|cell| {
            let Some(sprite) = cell.get_or_init(load_sprite) else { return };
            with_cairo(&mut self.frame, self.width, self.height, self.stride, |cr| {
                for (i, c) in comets.iter().enumerate() {
                    let head = head_of(c);
                    if head >= 0.0 {
                        draw_sprite_centered(cr, sprite, origin_x + i as f32 * STILETTO_COLUMN_SPACING_PX, head, pop_b);
                    }
                }
            });
        });

        let height = self.height as f32;
        if self.sweeping {
            self.sweep_drop += STILETTO_STEP_PX;
            if self.sweep_drop > height + STILETTO_STEP_PX {
                self.sweeping = false;
                for c in &mut self.comets {
                    c.drop = -(random01() * height);
                    c.ever_reset = true;
                    c.last_valid = false;
                }
            }
        } else {
            for c in &mut self.comets {
                c.drop += STILETTO_STEP_PX * c.speed;
                if c.drop > height && random01() < RESET_CHANCE {
                    c.drop = if c.ever_reset { -(random01() * height) } else { START_DROP };
                    c.ever_reset = true;
                    c.last_valid = false;
                }
            }
        }
    }

    pub fn frame(&self) -> Option<Frame<'_>> {
        (!self.frame.is_empty()).then_some(Frame { pixels: &self.frame, width: self.width, height: self.height, stride: self.stride })
    }
}
