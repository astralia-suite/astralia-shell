//! Text, icon and display-glyph rasterisation with Pango and cairo.

use std::cell::RefCell;
use std::collections::HashMap;

use pangocairo::pango::prelude::*;
use pangocairo::{cairo, pango};

use crate::render::app_fonts;
use crate::render::canvas::{FontFamily, TextStyle};

const FONT: &str = "ComicShannsMono Nerd Font";
const ICON_FONT: &str = "tabler-icons";
const GLYPH_FONT: &str = "Yuji Mai";

/// Straight-alpha RGBA in device pixels.
#[derive(Debug, Default)]
pub struct Raster {
    pub width: i32,
    pub height: i32,
    pub scale: i32,
    pub rgba: Vec<u8>,
}

/// Cairo ARGB32 (premultiplied BGRA in memory) to straight RGBA.
pub fn unpremultiply_bgra(src: &[u8], width: i32, height: i32, stride: i32) -> Vec<u8> {
    let (width, height, stride) = (width as usize, height as usize, stride as usize);
    let mut out = Vec::with_capacity(width * height * 4);
    for y in 0..height {
        for &[b, g, r, a] in src[y * stride..][..width * 4].as_chunks::<4>().0 {
            let (mut b, mut g, mut r, a) = (b as u32, g as u32, r as u32, a as u32);
            if a > 0 && a < 255 {
                let un = |c: u32| ((c * 255 + a / 2) / a).min(255);
                (r, g, b) = (un(r), un(g), un(b));
            }
            out.extend([r as u8, g as u8, b as u8, a as u8]);
        }
    }
    out
}

/// Tabler icons render unhinted with unhinted metrics; text and display glyphs use the default hinting.
fn font_options(icon: bool) -> Option<cairo::FontOptions> {
    let mut options = cairo::FontOptions::new().ok()?;
    options.set_antialias(cairo::Antialias::Gray);
    if icon {
        options.set_hint_style(cairo::HintStyle::None);
        options.set_hint_metrics(cairo::HintMetrics::Off);
    } else {
        options.set_hint_style(cairo::HintStyle::Default);
        options.set_hint_metrics(cairo::HintMetrics::Default);
    }
    Some(options)
}

fn text_font(px: i32, bold: bool) -> pango::FontDescription {
    let mut desc = pango::FontDescription::from_string(FONT);
    desc.set_weight(if bold { pango::Weight::Bold } else { pango::Weight::Normal });
    desc.set_absolute_size(px as f64 * pango::SCALE as f64);
    desc
}

fn layout(cr: &cairo::Context, options: &cairo::FontOptions, desc: &pango::FontDescription, text: &str) -> pango::Layout {
    cr.set_font_options(options);
    let layout = pangocairo::functions::create_layout(cr);
    layout.set_font_description(Some(desc));
    layout.set_text(text);
    layout
}

fn bound(layout: &pango::Layout, max_width: i32, wrap: bool) {
    if max_width > 0 {
        layout.set_width(max_width * pango::SCALE);
        if wrap {
            layout.set_wrap(pango::WrapMode::WordChar);
        } else {
            layout.set_ellipsize(pango::EllipsizeMode::End);
        }
    }
}

fn to_raster(mut surface: cairo::ImageSurface, scale: i32) -> Option<Raster> {
    surface.flush();
    let (width, height, stride) = (surface.width(), surface.height(), surface.stride());
    let data = surface.data().ok()?;
    let rgba = unpremultiply_bgra(&data, width, height, stride);
    Some(Raster { width, height, scale, rgba })
}

/// White text at `px` absolute size; equivalent to the `Text` family with no bounds.
#[cfg(test)]
pub fn rasterize(text: &str, px: i32, scale: i32) -> Raster {
    rasterize_text(text, &TextStyle { px, ..Default::default() }, scale)
}

/// White `text` in device pixels. Text is as wide as its ink and as tall as the font's ascent + descent (more when
/// wrapped), so it never jitters; icons and display glyphs are cut tight to their ink.
pub fn rasterize_text(text: &str, style: &TextStyle, scale: i32) -> Raster {
    app_fonts::register();
    let scale = scale.max(1);
    match style.family {
        FontFamily::Text => try_text(text, style, scale),
        FontFamily::Icon => try_glyph(text, ICON_FONT, true, style.px, scale),
        FontFamily::Glyph => try_glyph(text, GLYPH_FONT, false, style.px, scale),
    }
    .unwrap_or_default()
}

fn try_text(text: &str, style: &TextStyle, scale: i32) -> Option<Raster> {
    let desc = text_font(style.px, style.bold);
    let options = font_options(false)?;

    let measure = cairo::ImageSurface::create(cairo::Format::ARgb32, 1, 1).ok()?;
    let probe = layout(&cairo::Context::new(&measure).ok()?, &options, &desc, text);
    bound(&probe, style.max_width, style.wrap);
    let (ink, logical) = probe.pixel_extents();
    let metrics = pangocairo::FontMap::default().create_context().metrics(Some(&desc), None);
    // Ascent and descent are truncated separately.
    let line = metrics.ascent() / pango::SCALE + metrics.descent() / pango::SCALE;
    let (width, height) = (ink.width(), if style.wrap { line.max(logical.height()) } else { line });
    if width <= 0 || height <= 0 {
        return None;
    }

    let surface = cairo::ImageSurface::create(cairo::Format::ARgb32, width * scale, height * scale).ok()?;
    {
        let cr = cairo::Context::new(&surface).ok()?;
        cr.scale(scale as f64, scale as f64);
        let layout = layout(&cr, &options, &desc, text);
        bound(&layout, style.max_width, style.wrap);
        cr.set_source_rgba(1.0, 1.0, 1.0, 1.0);
        cr.move_to(-ink.x() as f64, 0.0);
        pangocairo::functions::show_layout(&cr, &layout);
    }
    to_raster(surface, scale)
}

/// One glyph of `family` with font fallback off, laid out at device size and cut to its ink.
fn try_glyph(text: &str, family: &str, icon: bool, px: i32, scale: i32) -> Option<Raster> {
    let mut desc = pango::FontDescription::from_string(family);
    desc.set_absolute_size(px as f64 * scale as f64 * pango::SCALE as f64);
    let options = font_options(icon)?;
    let attrs = pango::AttrList::new();
    attrs.insert(pango::AttrInt::new_fallback(false));
    let glyph_layout = |cr: &cairo::Context| {
        let l = layout(cr, &options, &desc, text);
        l.set_attributes(Some(&attrs));
        l
    };

    let measure = cairo::ImageSurface::create(cairo::Format::ARgb32, 1, 1).ok()?;
    let (ink, _) = glyph_layout(&cairo::Context::new(&measure).ok()?).extents();
    let unit = pango::SCALE as f64;
    let (ink_x, ink_y) = (ink.x() as f64 / unit, ink.y() as f64 / unit);
    let width = (ink.width() as f64 / unit).ceil() as i32;
    let height = (ink.height() as f64 / unit).ceil() as i32;
    if width <= 0 || height <= 0 {
        return None;
    }

    let surface = cairo::ImageSurface::create(cairo::Format::ARgb32, width, height).ok()?;
    {
        let cr = cairo::Context::new(&surface).ok()?;
        let layout = glyph_layout(&cr);
        cr.set_source_rgba(1.0, 1.0, 1.0, 1.0);
        cr.move_to(-ink_x, -ink_y);
        pangocairo::functions::show_layout(&cr, &layout);
    }
    to_raster(surface, scale)
}

/// The logical width of `M` in the text font, cached per size and weight.
pub fn advance_px(style: &TextStyle) -> f32 {
    thread_local! {
        static CACHE: RefCell<HashMap<(i32, bool), f32>> = RefCell::new(HashMap::new());
    }
    let key = (style.px, style.bold);
    if let Some(v) = CACHE.with_borrow(|c| c.get(&key).copied()) {
        return v;
    }
    app_fonts::register();
    let measured = (|| {
        let options = font_options(false)?;
        let surface = cairo::ImageSurface::create(cairo::Format::ARgb32, 1, 1).ok()?;
        let (_, logical) = layout(&cairo::Context::new(&surface).ok()?, &options, &text_font(style.px, style.bold), "M").pixel_extents();
        Some(logical.width() as f32)
    })();
    let v = measured.unwrap_or(0.0);
    if measured.is_some() {
        CACHE.with_borrow_mut(|c| c.insert(key, v));
    }
    v
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::render::icons;

    #[test]
    fn unpremultiply_pixel() {
        assert_eq!(unpremultiply_bgra(&[0, 64, 128, 128], 1, 1, 4), vec![255, 128, 0, 128]);
        assert_eq!(unpremultiply_bgra(&[0, 0, 0, 0], 1, 1, 4), vec![0, 0, 0, 0]);
    }

    #[test]
    fn rasterize_sizes() {
        let one = rasterize("12:34:56", 17, 1);
        assert!(one.width > 0 && one.height > 0);
        assert_eq!(one.rgba.len(), (one.width * one.height * 4) as usize);
        assert_eq!(rasterize("00:00:00", 17, 1).height, one.height);
        let two = rasterize("12:34:56", 17, 2);
        assert_eq!((two.width, two.height, two.scale), (one.width * 2, one.height * 2, 2));
        assert_eq!(rasterize("", 17, 1).width, 0);
    }

    fn style(family: FontFamily, px: i32) -> TextStyle {
        TextStyle { family, px, ..Default::default() }
    }

    #[test]
    fn icon_family_renders_tabler() {
        let icon = rasterize_text(icons::BELL, &style(FontFamily::Icon, 18), 1);
        assert!(icon.width > 0 && icon.height > 0);
        assert!(icon.rgba.chunks(4).any(|p| p[3] > 0));
        let families = pangocairo::FontMap::default().list_families();
        assert!(families.iter().any(|f| f.name() == "tabler-icons"), "tabler-icons is not registered");
        assert!(families.iter().any(|f| f.name() == "Yuji Mai"), "Yuji Mai is not registered");
        let two = rasterize_text(icons::BELL, &style(FontFamily::Icon, 18), 2);
        assert_eq!(two.scale, 2);
        assert!((two.width - icon.width * 2).abs() <= 2);
    }

    #[test]
    fn ellipsis_bounds_width() {
        let long = "the quick brown fox jumps over the lazy dog, twice over";
        let free = rasterize_text(long, &style(FontFamily::Text, 13), 1);
        assert!(free.width > 100);
        let cut = rasterize_text(long, &TextStyle { max_width: 100, ..style(FontFamily::Text, 13) }, 1);
        assert!(cut.width > 0 && cut.width <= 100, "width {}", cut.width);
        assert_eq!(cut.height, free.height);
    }

    #[test]
    fn wrap_adds_lines() {
        let long = "the quick brown fox jumps over the lazy dog, twice over";
        let one = rasterize_text("fox", &style(FontFamily::Text, 13), 1);
        let wrapped = rasterize_text(long, &TextStyle { max_width: 100, wrap: true, ..style(FontFamily::Text, 13) }, 1);
        assert!(wrapped.width <= 100);
        let lines = (wrapped.height as f32 / one.height as f32).round() as i32;
        assert!(lines >= 2, "height {} vs line {}", wrapped.height, one.height);
        assert!((wrapped.height - lines * one.height).abs() <= lines, "height {} is not {lines} lines of {}", wrapped.height, one.height);
    }

    #[test]
    fn fixed_metrics_per_family() {
        let text = style(FontFamily::Text, 13);
        assert_eq!(rasterize_text("ace", &text, 1).height, rasterize_text("ÁgjQ|", &text, 1).height);
        let bold = TextStyle { bold: true, ..text };
        assert_eq!(rasterize_text("ace", &bold, 1).height, rasterize_text("ÁgjQ|", &bold, 1).height);
        assert_eq!(rasterize_text("ace", &text, 1).height, rasterize("ace", 13, 1).height);
        assert!(advance_px(&text) > 0.0);
        assert!(advance_px(&TextStyle { px: 26, ..text }) > advance_px(&text));
    }
}
