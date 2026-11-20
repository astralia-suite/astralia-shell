use image::imageops::FilterType;

const INSTALLED_ASSETS: &str = "/usr/share/astralia-shell";

/// An absolute path as is; otherwise the installed asset, else the source tree's.
pub fn asset_path(name: &str) -> String {
    if name.starts_with('/') {
        return name.to_string();
    }
    let installed = format!("{INSTALLED_ASSETS}/{name}");
    if std::path::Path::new(&installed).exists() {
        return installed;
    }
    format!("{}/assets/{name}", env!("CARGO_MANIFEST_DIR"))
}

/// Straight RGBA rows, `width * height * 4` bytes.
pub struct Pixels {
    pub width: i32,
    pub height: i32,
    pub data: Vec<u8>,
    pub premultiplied: bool,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Placement {
    pub scale: f64,
    pub x: f64,
    pub y: f64,
}

fn place(scale: f64, image_width: i32, image_height: i32, area_width: i32, area_height: i32) -> Placement {
    Placement { scale, x: (f64::from(area_width) - f64::from(image_width) * scale) / 2.0, y: (f64::from(area_height) - f64::from(image_height) * scale) / 2.0 }
}

/// Fills the area, cropping the overflow evenly.
pub fn cover(image_width: i32, image_height: i32, area_width: i32, area_height: i32) -> Placement {
    let scale = (f64::from(area_width) / f64::from(image_width)).max(f64::from(area_height) / f64::from(image_height));
    place(scale, image_width, image_height, area_width, area_height)
}

/// Fits inside the area, centred.
pub fn contain(image_width: i32, image_height: i32, area_width: i32, area_height: i32) -> Placement {
    let scale = (f64::from(area_width) / f64::from(image_width)).min(f64::from(area_height) / f64::from(image_height));
    place(scale, image_width, image_height, area_width, area_height)
}

/// Raster images above this many pixels decode one at a time: a 10K JPEG holds several hundred MB
/// while it decodes, and two at once were the shell's peak.
const LARGE_PIXELS: u64 = 16_000_000;
static LARGE_DECODE: std::sync::Mutex<()> = std::sync::Mutex::new(());

/// Decodes a raster image and hands it to `use_image`; a large image keeps the decode slot until
/// `use_image` returns, so its full-size buffer is gone before the next large decode starts.
fn with_raster<T>(path: &str, use_image: impl FnOnce(image::DynamicImage) -> T) -> Result<T, String> {
    let reader = || image::ImageReader::open(path).map_err(|e| e.to_string())?.with_guessed_format().map_err(|e| e.to_string());
    let (width, height) = reader()?.into_dimensions().map_err(|e| e.to_string())?;
    let _slot = (u64::from(width) * u64::from(height) > LARGE_PIXELS).then(|| LARGE_DECODE.lock().unwrap_or_else(|e| e.into_inner()));
    let image = reader()?.decode().map_err(|e| e.to_string())?;
    Ok(use_image(image))
}

/// `image` scaled by `scale` when that shrinks it, as straight RGBA.
fn shrink_to_rgba(image: image::DynamicImage, scale: f64) -> Pixels {
    let (width, height) = (image.width(), image.height());
    let image = if scale < 1.0 {
        let fit = |side: u32| ((f64::from(side) * scale).round() as u32).max(1);
        image.resize_exact(fit(width), fit(height), FilterType::Triangle)
    } else {
        image
    };
    let rgba = image.into_rgba8();
    Pixels { width: rgba.width() as i32, height: rgba.height() as i32, data: rgba.into_raw(), premultiplied: false }
}

pub(crate) fn is_svg(path: &str) -> bool {
    use std::io::Read;
    let mut head = [0u8; 64];
    let Ok(n) = std::fs::File::open(path).and_then(|mut f| f.read(&mut head)) else { return false };
    let text = String::from_utf8_lossy(&head[..n]);
    text.contains("<svg") || text.contains("<?xml")
}

/// Renders an SVG fitted inside `fit_width` by `fit_height`, keeping its aspect (its own size when either is 0);
/// premultiplied RGBA.
pub fn decode_svg(path: &str, fit_width: i32, fit_height: i32) -> Result<Pixels, String> {
    let data = std::fs::read(path).map_err(|e| e.to_string())?;
    let tree = resvg::usvg::Tree::from_data(&data, &resvg::usvg::Options::default()).map_err(|e| format!("svg load failed: {e}"))?;
    let (width, height) = (f64::from(tree.size().width()), f64::from(tree.size().height()));
    if width <= 0.0 || height <= 0.0 {
        return Err("empty svg".into());
    }
    let scale = if fit_width > 0 && fit_height > 0 { (f64::from(fit_width) / width).min(f64::from(fit_height) / height) } else { 1.0 };
    let w = ((width * scale).round() as i32).max(1);
    let h = ((height * scale).round() as i32).max(1);
    let mut pixmap = resvg::tiny_skia::Pixmap::new(w as u32, h as u32).ok_or("svg too large")?;
    let transform = resvg::tiny_skia::Transform::from_scale((f64::from(w) / width) as f32, (f64::from(h) / height) as f32);
    resvg::render(&tree, transform, &mut pixmap.as_mut());
    Ok(Pixels { width: w, height: h, data: pixmap.take(), premultiplied: true })
}

/// Decodes a raster image, downscaled so its longer side is at most `max_side`; an SVG is rendered fitted inside a
/// `max_side` square (its own size when 0).
pub fn decode_pixels(path: &str, max_side: u32) -> Result<Pixels, String> {
    if is_svg(path) {
        let side = i32::try_from(max_side).unwrap_or(i32::MAX);
        return decode_svg(path, side, side);
    }
    with_raster(path, |image| {
        let longer = image.width().max(image.height());
        let scale = if max_side > 0 { f64::from(max_side) / f64::from(longer) } else { 1.0 };
        shrink_to_rgba(image, scale)
    })
}

/// An icon or asset: an SVG fitted inside an `svg_px` square, a raster image at its own size.
pub fn decode_image(path: &str, svg_px: i32) -> Result<Pixels, String> {
    if is_svg(path) { decode_svg(path, svg_px, svg_px) } else { decode_pixels(path, 0) }
}

/// Downscaled (never enlarged) until it just covers a `width` by `height` box; a raster image is
/// scaled before it becomes RGBA, so no full-size RGBA copy is made.
pub fn decode_cover(path: &str, width: i32, height: i32) -> Result<Pixels, String> {
    if is_svg(path) {
        let side = width.max(height);
        return decode_svg(path, side, side);
    }
    with_raster(path, |image| {
        let scale = (f64::from(width) / f64::from(image.width())).max(f64::from(height) / f64::from(image.height()));
        shrink_to_rgba(image, scale)
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn decode_downscales() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("wide.png");
        image::RgbaImage::from_pixel(1000, 500, image::Rgba([10, 20, 30, 255])).save(&path).unwrap();
        let pixels = decode_pixels(path.to_str().unwrap(), 256).unwrap();
        assert_eq!((pixels.width, pixels.height), (256, 128));
        assert_eq!(pixels.data.len(), 256 * 128 * 4);
        assert!(!pixels.premultiplied);
        assert_eq!(&pixels.data[..4], &[10, 20, 30, 255]);
        let small = decode_pixels(path.to_str().unwrap(), 4000).unwrap();
        assert_eq!((small.width, small.height), (1000, 500), "a smaller image is not scaled up");
        assert!(decode_pixels("/nonexistent.png", 256).is_err());
        let covered = decode_cover(path.to_str().unwrap(), 100, 100).unwrap();
        assert_eq!((covered.width, covered.height), (200, 100), "scaled down until it just covers the box");
        let full = decode_image(path.to_str().unwrap(), 32).unwrap();
        assert_eq!((full.width, full.height), (1000, 500), "a raster image keeps its size");
        std::fs::write(dir.path().join("broken.jpg"), b"\xFF\xD8 not really a jpeg").unwrap();
        assert!(decode_pixels(dir.path().join("broken.jpg").to_str().unwrap(), 256).is_err());
    }

    #[test]
    fn decode_svg_cases() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("icon.svg");
        std::fs::write(&path, "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'><rect width='24' height='24' fill='red'/></svg>").unwrap();
        let path = path.to_str().unwrap();
        let fitted = decode_pixels(path, 32).unwrap();
        assert_eq!((fitted.width, fitted.height), (32, 32));
        assert!(fitted.premultiplied);
        assert_eq!(&fitted.data[..4], &[255, 0, 0, 255]);
        let natural = decode_pixels(path, 0).unwrap();
        assert_eq!((natural.width, natural.height), (24, 24));

        let wide = dir.path().join("wide.svg");
        std::fs::write(&wide, "<?xml version='1.0'?>\n<svg xmlns='http://www.w3.org/2000/svg' width='40' height='20'><rect width='40' height='20' fill='blue'/></svg>").unwrap();
        let wide = wide.to_str().unwrap();
        let boxed = decode_svg(wide, 32, 32).unwrap();
        assert_eq!((boxed.width, boxed.height), (32, 16), "fitted inside the box, aspect kept");
        let by_height = decode_svg(wide, i32::MAX / 4, 30).unwrap();
        assert_eq!((by_height.width, by_height.height), (60, 30));
        std::fs::write(dir.path().join("bad.svg"), "<svg xmlns='http://www.w3.org/2000/svg'><rect").unwrap();
        assert!(decode_pixels(dir.path().join("bad.svg").to_str().unwrap(), 32).is_err());
    }

    #[test]
    fn placements() {
        let c = cover(200, 100, 100, 100);
        assert!((c.scale - 1.0).abs() < 1e-9 && (c.x + 50.0).abs() < 1e-9 && c.y.abs() < 1e-9);
        let c = contain(200, 100, 100, 100);
        assert!((c.scale - 0.5).abs() < 1e-9 && c.x.abs() < 1e-9 && (c.y - 25.0).abs() < 1e-9);
    }
}
