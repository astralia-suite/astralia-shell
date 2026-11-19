use std::cell::{Cell, RefCell};
use std::rc::{Rc, Weak};
use std::time::UNIX_EPOCH;

use crate::base::process::trim_heap;
use crate::base::signal::Signal;
use crate::config::settings::{Config, PaletteSource, default_wallpaper_path};
use crate::render::decode::decode_pixels;
use crate::render::oklab::{color_from_oklch, oklch_from_color};
use crate::render::palette_extract::{Extraction, Seed, extract_seeds};
use crate::render::theme::{Theme, theme_apply, theme_from_seed};
use crate::render::tokens::lerp_color;
use crate::service::compositor::Compositor;
use crate::service::palette_export::{export_palette, palette_export_path};
use crate::service::runtime;
use crate::service::settings::SettingsService;
use crate::service::wallpaper::{wallpaper_column_count_animated, wallpaper_column_override_animated, wallpaper_column_path_animated, wallpaper_image_for, wallpaper_is_video};

const SAMPLE_SIDE: u32 = 256;
const SAME_HUE_EPSILON: f32 = 0.5;
const SAME_TONE_EPSILON: f32 = 0.005;
const DEFAULT_PALETTE_TONE: f32 = 0.9;
const BORDER_MIN_LIGHTNESS: f32 = 0.85;

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ThemeSource {
    pub path: String,
    /// Modification time in nanoseconds since the epoch, `0` when unknown.
    pub modified: i64,
    pub fixed: bool,
}

/// The wallpaper the theme is drawn from: the region-capture column when set, else the monitor's
/// first column, else the first configured column, else the global wallpaper.
pub fn theme_wallpaper_path(cfg: &Config, monitor: &str) -> String {
    let animated = cfg.wallpaper_animated_enabled;
    if !cfg.capture_monitor.is_empty() && cfg.capture_column >= 0 && cfg.capture_column < wallpaper_column_count_animated(cfg, &cfg.capture_monitor, animated) {
        return if animated {
            wallpaper_column_override_animated(cfg, &cfg.capture_monitor, cfg.capture_column, true)
        } else {
            wallpaper_column_path_animated(cfg, &cfg.capture_monitor, cfg.capture_column, false)
        };
    }
    let first_column = |columns: &std::collections::BTreeMap<String, Vec<String>>| columns.values().filter_map(|c| c.first()).find(|p| !p.is_empty()).cloned();
    if animated {
        if !monitor.is_empty() {
            return wallpaper_column_override_animated(cfg, monitor, 0, true);
        }
        return first_column(&cfg.wallpaper_animated_columns).unwrap_or_default();
    }
    if !monitor.is_empty() {
        return wallpaper_image_for(cfg, monitor);
    }
    first_column(&cfg.wallpaper_columns).unwrap_or_else(|| if cfg.default_wallpaper { cfg.wallpaper_path.clone() } else { String::new() })
}

/// The fixed lavender palette used when the theme does not follow the wallpaper.
pub fn default_palette_extraction() -> Extraction {
    let seed = |hue, chroma, lightness| Seed { hue, chroma, lightness };
    Extraction { seeds: vec![seed(307.6, 0.111, 0.712), seed(355.3, 0.095, 0.808), seed(297.5, 0.127, 0.495), seed(306.7, 0.060, 0.902)], found: true, tone: DEFAULT_PALETTE_TONE }
}

pub fn theme_source(cfg: &Config, monitor: &str) -> ThemeSource {
    if cfg.palette_source == PaletteSource::DefaultPalette {
        return ThemeSource { fixed: true, ..Default::default() };
    }
    let path = theme_wallpaper_path(cfg, monitor);
    let modified = std::fs::metadata(&path)
        .and_then(|m| m.modified())
        .ok()
        .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
        .map_or(0, |d| i64::try_from(d.as_nanos()).unwrap_or(i64::MAX));
    ThemeSource { path, modified, fixed: false }
}

/// Small hue and tone shifts are the same theme; anything else is a new one.
pub fn extraction_differs(a: Option<&Extraction>, b: &Extraction) -> bool {
    let Some(a) = a else { return true };
    a.seeds.len() != b.seeds.len() || (a.tone - b.tone).abs() > SAME_TONE_EPSILON || a.seeds.iter().zip(&b.seeds).any(|(x, y)| (x.hue - y.hue).abs() > SAME_HUE_EPSILON)
}

fn extract_from_file(path: &str) -> Option<Extraction> {
    if path.is_empty() {
        return None;
    }
    let pixels = match decode_pixels(path, SAMPLE_SIDE) {
        Ok(pixels) => pixels,
        Err(e) => {
            ::log::info!("theme: could not decode {path}: {e}");
            return None;
        }
    };
    let extraction = extract_seeds(&pixels.data, pixels.width, pixels.height, pixels.premultiplied);
    if !extraction.found {
        ::log::info!("theme: no usable colour in {path}");
        return None;
    }
    Some(extraction)
}

/// Decodes off the loop thread; an unreadable wallpaper falls back to the bundled default, then to
/// nothing. A video themes from its first frame.
pub async fn resolve_theme(path: &str) -> Option<Extraction> {
    resolve_with(path, extract_from_file, extract_from_video).await
}

/// The first frame of a video, sampled at the theme's size.
fn extract_from_video(path: &str) -> Option<Extraction> {
    #[cfg(feature = "video")]
    {
        let Some(frame) = crate::service::media_decode::animate_decode_scaled(path, SAMPLE_SIDE as i32, SAMPLE_SIDE as i32) else {
            ::log::info!("theme: could not decode a frame of {path}");
            return None;
        };
        let extraction = extract_seeds(&frame.rgba, frame.width, frame.height, false);
        extraction.found.then_some(extraction)
    }
    #[cfg(not(feature = "video"))]
    {
        let _ = path;
        None
    }
}

async fn resolve_with(path: &str, extract: fn(&str) -> Option<Extraction>, video: fn(&str) -> Option<Extraction>) -> Option<Extraction> {
    let path = path.to_string();
    if wallpaper_is_video(&path) && !cfg!(feature = "video") {
        ::log::info!("theme: {path} is a video; keeping the current palette");
        return None;
    }
    blocking::unblock(move || {
        let extract = |p: &str| if wallpaper_is_video(p) { video(p) } else { extract(p) };
        // A decoder panic on a hostile file must not reach the awaiting task on the loop thread.
        let extraction = std::panic::catch_unwind(|| extract(&path).or_else(|| extract(&default_wallpaper_path()))).unwrap_or_else(|_| {
            ::log::error!("theme: decoding {path} panicked");
            None
        });
        trim_heap();
        extraction
    })
    .await
}

/// Active border: the accent lifted to at least `BORDER_MIN_LIGHTNESS`; inactive: accent toward base.
fn border_colors(theme: &Theme) -> (crate::render::tokens::Color, crate::render::tokens::Color) {
    let mut lch = oklch_from_color(theme.accent);
    lch.l = lch.l.max(BORDER_MIN_LIGHTNESS);
    (color_from_oklch(lch, theme.accent.a), lerp_color(theme.accent, theme.base, 0.6))
}

pub struct ThemeService {
    this: Weak<ThemeService>,
    compositor: Option<Rc<dyn Compositor>>,
    source: RefCell<Option<ThemeSource>>,
    applied: RefCell<Option<Extraction>>,
    generation: Cell<u64>,
    sync_borders: Cell<bool>,
    pub changed: Signal,
}

impl ThemeService {
    pub fn new(compositor: Option<Rc<dyn Compositor>>) -> Rc<ThemeService> {
        Rc::new_cyclic(|this| ThemeService {
            this: this.clone(),
            compositor,
            source: RefCell::new(None),
            applied: RefCell::new(None),
            generation: Cell::new(0),
            sync_borders: Cell::new(false),
            changed: Signal::default(),
        })
    }

    /// Follows the config: syncs now and on every settings change.
    pub fn start(settings: &Rc<SettingsService>, compositor: Rc<dyn Compositor>) -> Rc<ThemeService> {
        let service = ThemeService::new(Some(compositor));
        service.sync(&settings.config(), "");
        let (weak, settings_weak) = (Rc::downgrade(&service), Rc::downgrade(settings));
        settings.changed.connect(move |_| {
            if let (Some(service), Some(settings)) = (weak.upgrade(), settings_weak.upgrade()) {
                service.sync(&settings.config(), "");
            }
        });
        service
    }

    pub fn sync(&self, cfg: &Config, monitor: &str) {
        self.sync_borders.set(cfg.sync_compositor_borders);
        let source = theme_source(cfg, monitor);
        if self.source.borrow().as_ref() == Some(&source) {
            return;
        }
        *self.source.borrow_mut() = Some(source.clone());
        let generation = self.generation.get() + 1;
        self.generation.set(generation);
        if source.fixed {
            self.apply(&default_palette_extraction(), "default palette");
            return;
        }
        let weak = self.this.clone();
        runtime::spawn(async move {
            let extraction = resolve_theme(&source.path).await;
            if let (Some(service), Some(extraction)) = (weak.upgrade(), extraction)
                && service.generation.get() == generation
            {
                service.apply(&extraction, &source.path);
            }
        });
    }

    fn apply(&self, extraction: &Extraction, label: &str) {
        if !extraction_differs(self.applied.borrow().as_ref(), extraction) {
            return;
        }
        *self.applied.borrow_mut() = Some(extraction.clone());
        let theme = theme_from_seed(extraction);
        theme_apply(&theme);
        export_palette(&theme, &palette_export_path());
        if self.sync_borders.get()
            && let Some(compositor) = &self.compositor
        {
            let (active, inactive) = border_colors(&theme);
            compositor.apply_border_colors(active, inactive);
        }
        let a = theme.accent;
        let hex = |v: f32| (v.clamp(0.0, 1.0) * 255.0).round() as u8;
        ::log::info!("theme: {label} → accent #{:02x}{:02x}{:02x}", hex(a.r), hex(a.g), hex(a.b));
        self.changed.emit(&());
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::render::palette_extract::Seed;
    use std::cell::Cell;

    fn cols(v: &[&str]) -> Vec<String> {
        v.iter().map(|s| s.to_string()).collect()
    }

    fn lit(seeds: &[(f32, f32, f32)], tone: f32) -> Extraction {
        Extraction { seeds: seeds.iter().map(|&(hue, chroma, lightness)| Seed { hue, chroma, lightness }).collect(), found: true, tone }
    }

    #[test]
    fn theme_service_rules() {
        let mut cfg = Config::with_home("/home/t");
        cfg.wallpaper_path = "/global.png".into();
        assert_eq!(theme_wallpaper_path(&cfg, ""), "/global.png");
        cfg.wallpaper_path.clear();
        assert!(theme_wallpaper_path(&cfg, "").is_empty());
        cfg.wallpaper_columns.insert("DP-1".into(), cols(&["", "/second.png"]));
        cfg.wallpaper_columns.insert("DP-2".into(), cols(&["/dp2.png"]));
        assert_eq!(theme_wallpaper_path(&cfg, ""), "/dp2.png", "a column image beats the global one without a monitor");
        assert_eq!(theme_wallpaper_path(&cfg, "DP-1"), "", "a monitor with a blank column falls back to the blank global path");
        cfg.wallpaper_columns.insert("DP-1".into(), cols(&["/dp1.png"]));
        assert_eq!(theme_wallpaper_path(&cfg, "DP-1"), "/dp1.png");
        let mut animated = cfg.clone();
        animated.wallpaper_animated_enabled = true;
        animated.wallpaper_animated_columns.insert("DP-1".into(), cols(&["/clip.mp4", "/second.mp4"]));
        assert_eq!(theme_wallpaper_path(&animated, "DP-1"), "/clip.mp4");
        assert!(theme_wallpaper_path(&animated, "DP-2").is_empty());
        assert_eq!(theme_wallpaper_path(&animated, ""), "/clip.mp4");
        cfg.wallpaper_columns.insert("DP-1".into(), cols(&["/dp1.png", "/dp1b.png"]));
        cfg.wallpaper_column_counts.insert("DP-1".into(), 2);
        cfg.capture_monitor = "DP-1".into();
        cfg.capture_column = 1;
        assert_eq!(theme_wallpaper_path(&cfg, "DP-2"), "/dp1b.png", "region capture picks the chosen monitor column");
        cfg.capture_column = 5;
        assert_eq!(theme_wallpaper_path(&cfg, "DP-2"), "/dp2.png", "a missing capture column falls back to auto");
        cfg.capture_monitor.clear();
        cfg.wallpaper_columns.clear();

        let dir = tempfile::tempdir().unwrap();
        let file = dir.path().join("source.png");
        std::fs::write(&file, "x").unwrap();
        cfg.wallpaper_path = file.to_str().unwrap().into();
        assert!(theme_source(&cfg, "").fixed && theme_source(&cfg, "").path.is_empty(), "the default palette source is fixed");
        cfg.palette_source = PaletteSource::RegionCapture;
        let first = theme_source(&cfg, "");
        assert!(first.path == cfg.wallpaper_path && first.modified != 0);
        assert_eq!(first, theme_source(&cfg, ""));
        let later = std::fs::metadata(&file).unwrap().modified().unwrap() + std::time::Duration::from_secs(5);
        std::fs::File::options().write(true).open(&file).unwrap().set_modified(later).unwrap();
        assert_ne!(first, theme_source(&cfg, ""), "a changed mtime gives a different source");
        std::fs::remove_file(&file).unwrap();
        assert_eq!(theme_source(&cfg, "").modified, 0);
        assert!(!theme_source(&cfg, "").fixed);

        let keqing = default_palette_extraction();
        assert!(keqing.found && keqing.seeds.len() == 4 && keqing.seeds[0].hue > 300.0 && keqing.seeds[0].hue < 315.0);
        assert!(keqing.tone > 0.75);

        let a = lit(&[(200.0, 0.1, 0.7)], 0.5);
        let b = lit(&[(200.2, 0.1, 0.7)], 0.5);
        let c = lit(&[(250.0, 0.1, 0.7)], 0.5);
        let d = lit(&[(200.0, 0.1, 0.7), (300.0, 0.1, 0.7)], 0.5);
        let e = lit(&[(200.0, 0.1, 0.7), (300.0, 0.1, 0.7), (20.0, 0.1, 0.7)], 0.5);
        let f = lit(&[(200.0, 0.1, 0.7), (300.0, 0.1, 0.7), (60.0, 0.1, 0.7)], 0.5);
        assert!(extraction_differs(Some(&a), &lit(&[(200.0, 0.1, 0.7)], 0.2)), "a darker wallpaper is a new theme");
        assert!(!extraction_differs(Some(&a), &lit(&[(200.0, 0.1, 0.7)], 0.501)), "a negligible tone shift is the same theme");
        assert!(!extraction_differs(Some(&a), &b));
        assert!(extraction_differs(Some(&a), &c) && extraction_differs(Some(&a), &d) && extraction_differs(Some(&d), &e) && extraction_differs(Some(&e), &f));
        assert!(extraction_differs(None, &a));

        let service = ThemeService::new(None);
        let emitted = Rc::new(Cell::new(0));
        let em = emitted.clone();
        service.changed.connect(move |_| em.set(em.get() + 1));
        let defaults = Config::with_home("/home/t");
        assert!(std::path::Path::new(&default_wallpaper_path()).exists());
        service.sync(&defaults, "");
        assert_eq!(emitted.get(), 1, "the first sync applies the default palette at once");
        service.sync(&defaults, "");
        assert_eq!(emitted.get(), 1, "an unchanged source does nothing");
    }

    #[test]
    fn theme_decode_panic_stays_off_the_loop() {
        let got = futures_lite::future::block_on(resolve_with("/x/hostile.png", |_| panic!("decoder bug"), |_| None));
        assert!(got.is_none(), "a panicking decoder reads as no theme");
    }

    #[test]
    fn theme_resolve_falls_back() {
        let dir = tempfile::tempdir().unwrap();
        let corrupt = dir.path().join("broken.jpg");
        std::fs::write(&corrupt, b"\xFF\xD8 not really a jpeg").unwrap();
        let got = futures_lite::future::block_on(resolve_theme(corrupt.to_str().unwrap()));
        assert!(got.is_some_and(|e| e.found)); // the bundled default wallpaper has colour
        let first_frame = |_: &str| Some(default_palette_extraction());
        let video = futures_lite::future::block_on(resolve_with("/x/clip.mp4", extract_from_file, first_frame));
        if cfg!(feature = "video") {
            assert_eq!(video, Some(default_palette_extraction()), "a video themes from its first frame");
        } else {
            assert!(video.is_none(), "without video decoding a video path has no theme");
        }
    }
}
