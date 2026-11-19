//! Falling-glyph and comet rain simulations drawn into ARGB buffers.

pub mod matrix;
pub mod random;
pub mod stiletto;
pub mod wayland;

use pangocairo::cairo;

use crate::config::rain_config::FADE_ALPHA;

/// Premultiplied ARGB32 pixels of the latest tick.
pub struct Frame<'a> {
    pub pixels: &'a [u8],
    pub width: i32,
    pub height: i32,
    pub stride: i32,
}

/// Cleared ARGB32 buffer of at least one pixel and its stride.
pub fn argb_buffer(width: i32, height: i32) -> (Vec<u8>, i32) {
    let stride = cairo::Format::ARgb32.stride_for_width(width as u32).unwrap_or(width * 4);
    (vec![0; stride as usize * height as usize], stride)
}

/// Runs `draw` with a cairo context over `buffer`, flushed before returning.
pub fn with_cairo(buffer: &mut [u8], width: i32, height: i32, stride: i32, draw: impl FnOnce(&cairo::Context)) {
    if buffer.len() < stride as usize * height as usize {
        return;
    }
    // SAFETY: the surface and its context are finished before `buffer`'s borrow ends,
    // and the length check above covers `stride * height` bytes.
    let surface = unsafe { cairo::ImageSurface::create_for_data_unsafe(buffer.as_mut_ptr(), cairo::Format::ARgb32, width, height, stride) };
    let Ok(surface) = surface else { return };
    if let Ok(cr) = cairo::Context::new(&surface) {
        draw(&cr);
    }
    surface.flush();
    surface.finish();
}

/// Fades every premultiplied pixel by `FADE_ALPHA`.
pub fn decay(buffer: &mut [u8], width: i32, height: i32, stride: i32) {
    let step = ((255.0 * FADE_ALPHA) as i32).clamp(1, 255);
    for y in 0..height as usize {
        let row = &mut buffer[y * stride as usize..][..width as usize * 4];
        for px in row.as_chunks_mut::<4>().0 {
            let old = i32::from(px[3]);
            if old == 0 {
                continue;
            }
            let new = (old - step).max(0);
            if new == 0 {
                px.fill(0);
                continue;
            }
            for c in &mut px[..3] {
                *c = (i32::from(*c) * new / old) as u8;
            }
            px[3] = new as u8;
        }
    }
}
#[cfg(test)]
mod tests {
    use super::Frame;
    use super::matrix::Matrix;
    use super::random::seed;
    use super::stiletto::Stiletto;

    trait Rain: Default {
        fn rebuild(&mut self, width: i32, height: i32, async_speed: bool);
        fn tick(&mut self);
        fn frame(&self) -> Option<Frame<'_>>;
    }
    impl Rain for Matrix {
        fn rebuild(&mut self, w: i32, h: i32, a: bool) {
            Matrix::rebuild(self, w, h, a)
        }
        fn tick(&mut self) {
            Matrix::tick(self)
        }
        fn frame(&self) -> Option<Frame<'_>> {
            Matrix::frame(self)
        }
    }
    impl Rain for Stiletto {
        fn rebuild(&mut self, w: i32, h: i32, a: bool) {
            Stiletto::rebuild(self, w, h, a)
        }
        fn tick(&mut self) {
            Stiletto::tick(self)
        }
        fn frame(&self) -> Option<Frame<'_>> {
            Stiletto::frame(self)
        }
    }

    fn has_ink(pixels: &[u8]) -> bool {
        pixels.iter().skip(3).step_by(4).any(|&a| a != 0)
    }

    fn check_rain<R: Rain>(name: &str) {
        let mut rain = R::default();
        assert!(rain.frame().is_none(), "a rain with no rebuild has no frame");
        rain.rebuild(480, 600, false);
        let frame = rain.frame().expect(name);
        assert!(frame.width == 480 && frame.height == 600 && frame.stride >= 480 * 4, "{name}");
        assert!(!has_ink(frame.pixels), "a fresh buffer is transparent");

        seed(7);
        rain.rebuild(480, 600, true);
        for _ in 0..40 {
            rain.tick();
        }
        let first = rain.frame().unwrap().pixels.to_vec();
        assert!(has_ink(&first), "ticking draws");

        seed(7);
        rain.rebuild(480, 600, true);
        for _ in 0..40 {
            rain.tick();
        }
        assert!(rain.frame().unwrap().pixels == first.as_slice(), "the same seed gives the same frames");

        rain.rebuild(200, 100, false);
        let f = rain.frame().unwrap();
        assert!(f.width == 200 && f.height == 100, "a rebuild resizes the buffer");
        rain.rebuild(0, 0, false);
        let f = rain.frame().unwrap();
        assert!(f.width == 1 && f.height == 1, "a degenerate size is clamped");
    }

    #[test]
    fn check_rain_buffer() {
        check_rain::<Matrix>("the matrix buffer has the requested size");
        check_rain::<Stiletto>("the stiletto buffer has the requested size");
    }
}
