//! Decoded images to textures.

use super::renderer::{Renderer, Texture};
use crate::render::decode::Pixels;

/// Uploads decoded pixels with mipmaps at scale 1, unpremultiplying first when needed.
pub fn upload_pixels(renderer: &Renderer, mut pixels: Pixels) -> Option<Texture> {
    if pixels.premultiplied {
        for p in pixels.data.as_chunks_mut::<4>().0 {
            let a = p[3] as u32;
            if a != 0 && a != 255 {
                for c in &mut p[..3] {
                    *c = (*c as u32 * 255 / a).min(255) as u8;
                }
            }
        }
    }
    renderer.upload_rgba_mipmapped(pixels.width, pixels.height, 1, &pixels.data)
}
