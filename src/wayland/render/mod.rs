pub mod arc_gauge;
pub mod egl;
pub mod gl_canvas;
pub mod image;
pub mod layer_window;
pub mod overlay;
pub mod overlay_panel;
pub mod popup_window;
pub mod renderer;
pub mod texture_cache;
pub mod wallpaper_column;
pub mod text;
pub mod thunder;
pub mod toplevel_window;
#[cfg(feature = "video")]
pub mod video_texture;

/// Device-pixel buffer size for a logical size at an integer scale (below 1 counts as 1).
pub fn buffer_size(width: i32, height: i32, scale: i32) -> (i32, i32) {
    let scale = scale.max(1);
    (width * scale, height * scale)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn buffer_size_scales() {
        assert_eq!(buffer_size(1880, 40, 1), (1880, 40));
        assert_eq!(buffer_size(1880, 40, 2), (3760, 80));
        assert_eq!(buffer_size(1880, 40, 0), (1880, 40));
    }
}
