use std::path::{Path, PathBuf};

use crate::render::oklab::{color_from_oklch, contrast_ratio, oklch_from_color};
use crate::render::theme::Theme;
use crate::render::tokens::{Color, lerp_color};

const BRIGHT_MIX: f32 = 0.2;
const MIN_CONTRAST: f32 = 4.5;
const LIGHTNESS_STEP: f32 = 0.01;
const MAX_STEPS: i32 = 100;

fn hex_of(c: Color) -> u32 {
    let channel = |v: f32| (v.clamp(0.0, 1.0) * 255.0).round() as u32;
    channel(c.r) << 16 | channel(c.g) << 8 | channel(c.b)
}

/// Moves the lightness away from the background until the colour reads on it.
fn readable(c: Color, theme: &Theme) -> Color {
    let mut lch = oklch_from_color(c);
    let step = if oklch_from_color(theme.text).l > oklch_from_color(theme.accent_container).l { LIGHTNESS_STEP } else { -LIGHTNESS_STEP };
    let mut out = c;
    for _ in 0..MAX_STEPS {
        if contrast_ratio(out, theme.accent_container) >= MIN_CONTRAST {
            break;
        }
        lch.l = (lch.l + step).clamp(0.0, 1.0);
        out = color_from_oklch(lch, c.a);
    }
    out
}

fn bright(c: Color, theme: &Theme) -> Color {
    lerp_color(c, theme.text, BRIGHT_MIX)
}

fn ansi_colors(theme: &Theme) -> [Color; 16] {
    let critical = readable(theme.critical, theme);
    let green = readable(theme.pop_a, theme);
    let yellow = readable(theme.warn, theme);
    let blue = readable(theme.pop_b, theme);
    let magenta = readable(theme.accent, theme);
    let cyan = readable(theme.accent_alt, theme);
    [
        theme.ink,
        critical,
        green,
        yellow,
        blue,
        magenta,
        cyan,
        readable(theme.text_muted, theme),
        readable(theme.text_dim, theme),
        bright(critical, theme),
        bright(green, theme),
        bright(yellow, theme),
        bright(blue, theme),
        bright(magenta, theme),
        bright(cyan, theme),
        theme.text,
    ]
}

pub fn palette_export_path_in(state_home: &str, home: &str) -> PathBuf {
    let name = "palette";
    if state_home.starts_with('/') {
        return PathBuf::from(state_home).join("astralia").join(name);
    }
    if !home.is_empty() {
        return PathBuf::from(home).join(".local/state/astralia").join(name);
    }
    PathBuf::new()
}

/// `$XDG_STATE_HOME/astralia/palette`.
pub fn palette_export_path() -> PathBuf {
    let var = |name| std::env::var(name).unwrap_or_default();
    palette_export_path_in(&var("XDG_STATE_HOME"), &var("HOME"))
}

/// Terminal colours as `key=rrggbb` lines.
pub fn palette_export_text(theme: &Theme) -> String {
    let mut text = format!("fg={:06x}\nbg={:06x}\ncursor={:06x}\n", hex_of(theme.text), hex_of(theme.accent_container), hex_of(readable(theme.accent, theme)));
    for (i, c) in ansi_colors(theme).iter().enumerate() {
        text += &format!("color{i}={:06x}\n", hex_of(*c));
    }
    text
}

pub fn palette_export_css(theme: &Theme) -> String {
    let entries = [
        ("astralia-ink", theme.ink, ""),
        ("astralia-paper", theme.paper, ""),
        ("astralia-surface", theme.surface, ""),
        ("astralia-surface-alt", theme.surface_alt, ""),
        ("astralia-accent", theme.accent, ""),
        ("astralia-accent-alt", theme.accent_alt, ""),
        ("astralia-pop-a", theme.pop_a, ""),
        ("astralia-pop-b", theme.pop_b, ""),
        ("astralia-pop-c", theme.pop_c, ""),
        ("astralia-critical", theme.critical, ""),
        ("astralia-warn", theme.warn, ""),
        ("astralia-text-muted", theme.text_muted, ""),
        ("zen-primary-color", theme.accent, " !important"),
        ("zen-colors-primary", theme.text, " !important"),
        ("zen-colors-secondary", theme.surface_alt, " !important"),
        ("zen-colors-tertiary", theme.paper, " !important"),
        ("zen-colors-border", theme.surface_alt, " !important"),
        ("zen-colors-input-bg", theme.surface, " !important"),
    ];
    let mut css = String::from(":root {\n");
    for (name, c, flag) in entries {
        css += &format!("  --{name}: #{:06x}{flag};\n", hex_of(c));
    }
    css + "}\n"
}

fn write_atomic(path: &Path, text: &str) -> bool {
    let mut temp = path.as_os_str().to_owned();
    temp.push(".tmp");
    if let Err(e) = std::fs::write(&temp, text) {
        ::log::error!("palette export: cannot write {}: {e}", Path::new(&temp).display());
        return false;
    }
    if let Err(e) = std::fs::rename(&temp, path) {
        ::log::error!("palette export: cannot rename onto {}: {e}", path.display());
        return false;
    }
    true
}

/// Writes `path` and `path.css`, creating the directory first.
pub fn export_palette(theme: &Theme, path: &Path) -> bool {
    let Some(dir) = path.parent().filter(|_| !path.as_os_str().is_empty()) else { return false };
    if let Err(e) = std::fs::create_dir_all(dir) {
        ::log::error!("palette export: cannot create {}: {e}", dir.display());
        return false;
    }
    let mut css = path.as_os_str().to_owned();
    css.push(".css");
    let plain = write_atomic(path, &palette_export_text(theme));
    let styled = write_atomic(Path::new(&css), &palette_export_css(theme));
    plain && styled
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::render::oklab::contrast_ratio;
    use crate::render::palette_extract::{Extraction, Seed};
    use crate::render::theme::theme_from_seed;

    fn base_extraction() -> Extraction {
        Extraction { seeds: vec![Seed { hue: 195.0, chroma: 0.09, lightness: 0.75 }], found: true, tone: 0.9 }
    }

    fn hex(c: Color) -> String {
        format!("{:02x}{:02x}{:02x}", (c.r * 255.0 + 0.5) as i32, (c.g * 255.0 + 0.5) as i32, (c.b * 255.0 + 0.5) as i32)
    }

    #[test]
    fn palette_export() {
        let theme = theme_from_seed(&base_extraction());
        let text = palette_export_text(&theme);
        assert_eq!(text.matches('\n').count(), 19, "the export lists fg, bg, cursor and sixteen ANSI colors");
        assert!(text.starts_with("fg="));
        assert!(text.contains(&format!("bg={}\n", hex(theme.accent_container))));
        assert!(text.contains(&format!("color0={}\n", hex(theme.ink))));
        assert!(text.contains(&format!("color15={}\n", hex(theme.text))));
        assert!(text.contains(&format!("fg={}\n", hex(theme.text))));
        for tone in [0.9, 0.5, 0.2] {
            let themed = theme_from_seed(&Extraction { tone, ..base_extraction() });
            let themed_text = palette_export_text(&themed);
            for i in 1..=14 {
                let key = format!("\ncolor{i}=");
                let at = themed_text.find(&key).unwrap() + key.len();
                let value = u32::from_str_radix(&themed_text[at..at + 6], 16).unwrap();
                let c = Color { r: (value >> 16 & 0xff) as f32 / 255.0, g: (value >> 8 & 0xff) as f32 / 255.0, b: (value & 0xff) as f32 / 255.0, a: 1.0 };
                assert!(contrast_ratio(c, themed.accent_container) >= 4.4, "every ANSI text color is readable on the background");
            }
            assert!(!themed_text.contains(&format!("color15={}\n", hex(themed.accent_container))), "bright white is never the background color");
            assert!(contrast_ratio(themed.text, themed.accent_container) >= 4.5 && themed_text.contains(&format!("fg={}\n", hex(themed.text))));
        }

        let dir = tempfile::tempdir().unwrap();
        let file = dir.path().join("nested/palette");
        assert!(export_palette(&theme, &file), "export creates the missing directories");
        assert_eq!(std::fs::read_to_string(&file).unwrap(), text);
        assert!(!dir.path().join("nested/palette.tmp").exists(), "no temporary file is left behind");
        let css = palette_export_css(&theme);
        assert!(css.starts_with(":root {") && css.ends_with('\n'));
        assert!(css.contains(&format!("--astralia-accent: #{};", hex(theme.accent))));
        assert!(css.contains(&format!("--zen-primary-color: #{} !important;", hex(theme.accent))));
        assert!(css.contains(&format!("--zen-colors-tertiary: #{} !important;", hex(theme.paper))));
        assert_eq!(std::fs::read_to_string(dir.path().join("nested/palette.css")).unwrap(), css);
        let other = Theme { ink: theme.paper, ..theme };
        assert!(export_palette(&other, &file), "export overwrites an existing file");
        assert_eq!(std::fs::read_to_string(&file).unwrap(), palette_export_text(&other));
        assert!(!export_palette(&theme, std::path::Path::new("")), "an empty path is refused");
        assert_eq!(palette_export_path_in("/state", "/home/u"), std::path::PathBuf::from("/state/astralia/palette"));
        assert_eq!(palette_export_path_in("", "/home/u"), std::path::PathBuf::from("/home/u/.local/state/astralia/palette"));
        assert_eq!(palette_export_path_in("relative", ""), std::path::PathBuf::new());
    }
}
