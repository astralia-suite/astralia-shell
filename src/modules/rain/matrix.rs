//! Katakana matrix rain.

use std::sync::LazyLock;

use pangocairo::cairo;

use super::random::{random01, random_range};
use super::{Frame, argb_buffer, decay, with_cairo};
use crate::config::rain_config::{
    ASYNC_SPEED_MAX, ASYNC_SPEED_MIN, MATRIX_BOLD_CHANCE, MATRIX_CELL_HEIGHT, MATRIX_CELL_WIDTH, MATRIX_FONT_PX,
    RESET_CHANCE,
};
use crate::render::theme::palette;
use crate::render::tokens::Color;

static GLYPH_POOL: LazyLock<Vec<char>> = LazyLock::new(|| {
    let mut pool: Vec<char> = (0xFF66..=0xFF9D).filter_map(char::from_u32).collect();
    for _ in 0..2 {
        pool.extend("1234567890".chars());
    }
    for _ in 0..4 {
        pool.extend("-=*_+|:<>\"".chars());
    }
    pool
});

const START_DROP: f32 = -2.0;

fn draw_glyph_centered(cr: &cairo::Context, glyph: char, cell_x: f32, cell_y: f32, bold: bool, color: Color) {
    let weight = if bold { cairo::FontWeight::Bold } else { cairo::FontWeight::Normal };
    cr.select_font_face("Noto Sans CJK JP", cairo::FontSlant::Normal, weight);
    cr.set_font_size(f64::from(MATRIX_FONT_PX));
    let mut utf8 = [0u8; 4];
    let text = glyph.encode_utf8(&mut utf8);
    let Ok(extents) = cr.text_extents(text) else { return };
    let tx = cell_x + (MATRIX_CELL_WIDTH - extents.width() as f32) / 2.0 - extents.x_bearing() as f32;
    let ty = cell_y + (MATRIX_CELL_HEIGHT - extents.height() as f32) / 2.0 - extents.y_bearing() as f32;
    cr.set_source_rgba(f64::from(color.r), f64::from(color.g), f64::from(color.b), f64::from(color.a));
    cr.move_to(f64::from(tx.round()), f64::from(ty.round()));
    let _ = cr.show_text(text);
}

fn random_glyph() -> char {
    let pool = &*GLYPH_POOL;
    let idx = ((random01() * pool.len() as f32) as usize).min(pool.len() - 1);
    pool[idx]
}

#[derive(Clone, Copy)]
struct Column {
    drop: f32,
    ever_reset: bool,
    last_head_drop: f32,
    last_glyph: char,
    last_head_valid: bool,
    speed: f32,
    accum: f32,
}

#[derive(Default)]
pub struct Matrix {
    width: i32,
    height: i32,
    column_count: i32,
    row_count: i32,
    offset_x: f32,
    offset_y: f32,
    sweeping: bool,
    sweep_drop: f32,
    buffer: Vec<u8>,
    stride: i32,
    columns: Vec<Column>,
}

impl Matrix {
    pub fn rebuild(&mut self, width: i32, height: i32, async_speed: bool) {
        self.width = width.max(1);
        self.height = height.max(1);
        self.column_count = ((self.width as f32 / (MATRIX_CELL_WIDTH * 2.0)) as i32).max(1);
        self.row_count = ((self.height as f32 / MATRIX_CELL_HEIGHT) as i32).max(1);
        self.columns = (0..self.column_count)
            .map(|_| Column {
                drop: START_DROP,
                ever_reset: false,
                last_head_drop: 0.0,
                last_glyph: ' ',
                last_head_valid: false,
                speed: if async_speed { random_range(ASYNC_SPEED_MIN, ASYNC_SPEED_MAX) } else { 1.0 },
                accum: 0.0,
            })
            .collect();
        let content_width = self.column_count as f32 * MATRIX_CELL_WIDTH * 2.0 - MATRIX_CELL_WIDTH;
        self.offset_x = (self.width as f32 - content_width) / 2.0;
        self.offset_y = (self.height as f32 - self.row_count as f32 * MATRIX_CELL_HEIGHT) / 2.0;
        self.sweeping = true;
        self.sweep_drop = START_DROP;
        (self.buffer, self.stride) = argb_buffer(self.width, self.height);
    }

    pub fn tick(&mut self) {
        if self.buffer.is_empty() || self.columns.is_empty() {
            return;
        }
        decay(&mut self.buffer, self.width, self.height, self.stride);
        let theme = palette();
        let (sweeping, sweep_drop, offset_x, offset_y) = (self.sweeping, self.sweep_drop, self.offset_x, self.offset_y);
        let columns = &mut self.columns;
        with_cairo(&mut self.buffer, self.width, self.height, self.stride, |cr| {
            let mut options = cairo::FontOptions::new().ok();
            if let Some(options) = options.as_mut() {
                options.set_antialias(cairo::Antialias::Gray);
                cr.set_font_options(options);
            }
            for (c, col) in columns.iter_mut().enumerate() {
                let x = offset_x + c as f32 * MATRIX_CELL_WIDTH * 2.0;
                if col.last_head_valid {
                    let last_y = offset_y + col.last_head_drop * MATRIX_CELL_HEIGHT;
                    draw_glyph_centered(cr, col.last_glyph, x, last_y, false, theme.accent);
                }
                let head = if sweeping { sweep_drop } else { col.drop };
                if head >= 0.0 {
                    let y = offset_y + head * MATRIX_CELL_HEIGHT;
                    let bold = random01() < MATRIX_BOLD_CHANCE;
                    let glyph = random_glyph();
                    draw_glyph_centered(cr, glyph, x, y, bold, theme.pop_b);
                    col.last_glyph = glyph;
                    col.last_head_drop = head;
                    col.last_head_valid = true;
                } else {
                    col.last_head_valid = false;
                }
            }
        });

        let (height, rows) = (self.height as f32, self.row_count as f32);
        if self.sweeping {
            self.sweep_drop += 1.0;
            if self.sweep_drop * MATRIX_CELL_HEIGHT > height + MATRIX_CELL_HEIGHT {
                self.sweeping = false;
                for col in &mut self.columns {
                    col.drop = -(random01() * rows);
                    col.ever_reset = true;
                    col.last_head_valid = false;
                }
            }
        } else {
            for col in &mut self.columns {
                col.accum += col.speed;
                if col.accum < 1.0 {
                    continue;
                }
                col.accum -= 1.0;
                col.drop += 1.0;
                if col.drop * MATRIX_CELL_HEIGHT > height && random01() < RESET_CHANCE {
                    col.drop = if col.ever_reset { -(random01() * rows) } else { START_DROP };
                    col.ever_reset = true;
                    col.last_head_valid = false;
                }
            }
        }
    }

    pub fn frame(&self) -> Option<Frame<'_>> {
        (!self.buffer.is_empty()).then_some(Frame { pixels: &self.buffer, width: self.width, height: self.height, stride: self.stride })
    }
}
