//! The bamboo window's content: the grove, ticked and uploaded each tick interval.

use std::time::Instant;

use crate::backend::wayland::render::renderer::Renderer;
use crate::config::bamboo_config::TICK_INTERVAL_MS;
use crate::modules::bamboo::grow::Grow;
use crate::modules::rain::wayland::rain::FrameTexture;
use crate::render::theme::palette;

pub struct Bamboo {
    grow: Grow,
    built: (i32, i32),
    last_tick: Instant,
    texture: FrameTexture,
}

impl Default for Bamboo {
    fn default() -> Self {
        Bamboo { grow: Grow::default(), built: (0, 0), last_tick: Instant::now(), texture: FrameTexture::default() }
    }
}

impl Bamboo {
    fn rebuild(&mut self, renderer: &Renderer, size: (i32, i32)) {
        self.grow.rebuild(size.0, size.1);
        self.built = size;
        self.last_tick = Instant::now();
        self.texture.upload(renderer, self.grow.frame());
    }

    pub fn opened(&mut self) {
        self.built = (0, 0);
    }

    pub fn closed(&mut self, renderer: &Renderer) {
        self.texture.clear(renderer);
    }

    pub fn paint(&mut self, renderer: &Renderer, size: (i32, i32)) {
        let (w, h) = (size.0 as f32, size.1 as f32);
        renderer.draw_rect(0.0, 0.0, w, h, palette().window_backdrop.rgba());
        if size != self.built {
            self.rebuild(renderer, size);
        }
        let now = Instant::now();
        if now.saturating_duration_since(self.last_tick).as_secs_f32() * 1000.0 >= TICK_INTERVAL_MS {
            self.grow.tick();
            self.texture.upload(renderer, self.grow.frame());
            self.last_tick = now;
        }
        self.texture.draw(renderer, w, h);
    }
}
