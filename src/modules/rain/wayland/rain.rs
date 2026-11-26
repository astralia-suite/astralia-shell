//! The rain window's content: the active sim, ticked and uploaded each fall interval.

use std::time::Instant;

use crate::wayland::render::renderer::{Renderer, Texture};
use crate::wayland::render::text::unpremultiply_bgra;
use crate::config::rain_config::FALL_INTERVAL_MS;
use crate::config::settings::{RainMode, RainParams};
use crate::modules::rain::Frame;
use crate::modules::rain::matrix::Matrix;
use crate::modules::rain::stiletto::Stiletto;
use crate::render::theme::palette;

const WHITE: [f32; 4] = [1.0, 1.0, 1.0, 1.0];

/// A sim frame as a texture drawn over the whole window.
#[derive(Default)]
pub struct FrameTexture(Option<Texture>);

impl FrameTexture {
    pub fn upload(&mut self, renderer: &Renderer, frame: Option<Frame>) {
        let Some(frame) = frame else { return };
        let rgba = unpremultiply_bgra(frame.pixels, frame.width, frame.height, frame.stride);
        if let Some(tex) = self.0.as_ref().filter(|t| t.width == frame.width && t.height == frame.height) {
            renderer.update_rgba(tex, &rgba);
            return;
        }
        self.clear(renderer);
        self.0 = renderer.upload_rgba(frame.width, frame.height, 1, &rgba);
    }

    pub fn draw(&self, renderer: &Renderer, width: f32, height: f32) {
        if let Some(tex) = &self.0 {
            renderer.draw_texture_rect(0.0, 0.0, width, height, tex, WHITE);
        }
    }

    pub fn clear(&mut self, renderer: &Renderer) {
        if let Some(tex) = self.0.take() {
            renderer.delete_texture(tex);
        }
    }
}

pub struct Rain {
    matrix: Matrix,
    stiletto: Stiletto,
    params: RainParams,
    built: (i32, i32),
    last_tick: Instant,
    texture: FrameTexture,
}

impl Rain {
    pub fn new(params: &RainParams) -> Rain {
        Rain {
            matrix: Matrix::default(),
            stiletto: Stiletto::default(),
            params: params.clone(),
            built: (0, 0),
            last_tick: Instant::now(),
            texture: FrameTexture::default(),
        }
    }

    /// Stores the params; true when an open window must rebuild (the next paint does it).
    pub fn apply_params(&mut self, params: &RainParams, open: bool) -> bool {
        let changed = *params != self.params;
        self.params = params.clone();
        if changed && open {
            self.built = (0, 0);
        }
        changed && open
    }

    fn rebuild(&mut self, renderer: &Renderer, size: (i32, i32)) {
        self.texture.clear(renderer);
        match self.params.mode {
            RainMode::Matrix => self.matrix.rebuild(size.0, size.1, self.params.async_speed),
            RainMode::Stiletto => self.stiletto.rebuild(size.0, size.1, self.params.async_speed),
        }
        self.built = size;
        self.last_tick = Instant::now();
    }

    pub fn opened(&mut self) {
        self.built = (0, 0);
    }

    /// Drops the texture and both sims' buffers; `opened` rebuilds them at the next paint.
    pub fn closed(&mut self, renderer: &Renderer) {
        self.texture.clear(renderer);
        self.matrix = Matrix::default();
        self.stiletto = Stiletto::default();
        self.built = (0, 0);
    }

    pub fn paint(&mut self, renderer: &Renderer, size: (i32, i32)) {
        let (w, h) = (size.0 as f32, size.1 as f32);
        renderer.draw_rect(0.0, 0.0, w, h, palette().window_backdrop.rgba());
        if size != self.built {
            self.rebuild(renderer, size);
        }
        let now = Instant::now();
        if now.saturating_duration_since(self.last_tick).as_secs_f32() * 1000.0 >= FALL_INTERVAL_MS {
            let frame = match self.params.mode {
                RainMode::Matrix => {
                    self.matrix.tick();
                    self.matrix.frame()
                }
                RainMode::Stiletto => {
                    self.stiletto.tick();
                    self.stiletto.frame()
                }
            };
            self.texture.upload(renderer, frame);
            self.last_tick = now;
        }
        self.texture.draw(renderer, w, h);
    }
}

#[cfg(test)]
mod tests {
    use super::Rain;
    use crate::config::settings::{RainMode, RainParams};

    #[test]
    fn rain_params_rebuild() {
        let matrix = RainParams::default();
        let stiletto = RainParams { mode: RainMode::Stiletto, async_speed: false };
        let async_matrix = RainParams { mode: RainMode::Matrix, async_speed: true };
        let mut rain = Rain::new(&matrix);
        assert!(!rain.apply_params(&matrix, true), "the same params do not rebuild");
        assert!(rain.apply_params(&stiletto, true), "a new mode rebuilds an open window");
        assert!(rain.apply_params(&async_matrix, true), "a new async flag rebuilds");
        assert!(!rain.apply_params(&stiletto, false), "a closed window only stores them");
        assert!(!rain.apply_params(&stiletto, true), "and the stored params count as current");
    }
}
