use std::collections::BTreeMap;
use std::path::Path;

use crate::config::file::expand_home;
use crate::config::settings::Config;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WallpaperSlot {
    Still,
    Animated,
    Lock,
}

type Columns = BTreeMap<String, Vec<String>>;

fn column_map(cfg: &Config, slot: WallpaperSlot) -> &Columns {
    match slot {
        WallpaperSlot::Still => &cfg.wallpaper_columns,
        WallpaperSlot::Animated => &cfg.wallpaper_animated_columns,
        WallpaperSlot::Lock => &cfg.lock_wallpaper_columns,
    }
}

fn column_map_mut(cfg: &mut Config, slot: WallpaperSlot) -> &mut Columns {
    match slot {
        WallpaperSlot::Still => &mut cfg.wallpaper_columns,
        WallpaperSlot::Animated => &mut cfg.wallpaper_animated_columns,
        WallpaperSlot::Lock => &mut cfg.lock_wallpaper_columns,
    }
}

fn fill_mode_map(cfg: &Config, slot: WallpaperSlot) -> &Columns {
    match slot {
        WallpaperSlot::Still => &cfg.wallpaper_fill_modes,
        WallpaperSlot::Animated => &cfg.wallpaper_animated_fill_modes,
        WallpaperSlot::Lock => &cfg.lock_wallpaper_fill_modes,
    }
}

fn fill_mode_map_mut(cfg: &mut Config, slot: WallpaperSlot) -> &mut Columns {
    match slot {
        WallpaperSlot::Still => &mut cfg.wallpaper_fill_modes,
        WallpaperSlot::Animated => &mut cfg.wallpaper_animated_fill_modes,
        WallpaperSlot::Lock => &mut cfg.lock_wallpaper_fill_modes,
    }
}

fn count_map_mut(cfg: &mut Config, slot: WallpaperSlot) -> &mut BTreeMap<String, i32> {
    match slot {
        WallpaperSlot::Still => &mut cfg.wallpaper_column_counts,
        WallpaperSlot::Animated => &mut cfg.wallpaper_animated_column_counts,
        WallpaperSlot::Lock => &mut cfg.lock_wallpaper_column_counts,
    }
}

pub fn wallpaper_slot_of(animated: bool) -> WallpaperSlot {
    if animated { WallpaperSlot::Animated } else { WallpaperSlot::Still }
}

pub fn wallpaper_lock_slot(cfg: &Config) -> WallpaperSlot {
    if cfg.lock_wallpaper_custom { WallpaperSlot::Lock } else { WallpaperSlot::Still }
}

pub fn wallpaper_slot_dir(cfg: &Config, slot: WallpaperSlot) -> &str {
    match slot {
        WallpaperSlot::Still => &cfg.wallpaper_dir,
        WallpaperSlot::Animated => &cfg.wallpaper_animated_dir,
        WallpaperSlot::Lock => &cfg.lock_wallpaper_dir,
    }
}

fn extension(path: &str) -> String {
    Path::new(path).extension().map(|e| format!(".{}", e.to_string_lossy().to_lowercase())).unwrap_or_default()
}

pub fn wallpaper_is_image(path: &str) -> bool {
    matches!(extension(path).as_str(), ".png" | ".jpg" | ".jpeg" | ".jfif" | ".bmp" | ".svg")
}

pub fn wallpaper_is_video(path: &str) -> bool {
    matches!(extension(path).as_str(), ".mp4" | ".webm" | ".mkv")
}

/// By extension, then by file name, ignoring case.
fn picker_key(path: &str) -> (String, String) {
    (extension(path), Path::new(path).file_name().map(|n| n.to_string_lossy().to_lowercase()).unwrap_or_default())
}

pub fn wallpaper_picker_less(a: &str, b: &str) -> bool {
    picker_key(a) < picker_key(b)
}

/// Matching files in `dir` and its direct subfolders, in picker order, at most `limit`.
pub fn wallpaper_list_files(dir: &str, animated: bool, limit: usize) -> Vec<String> {
    let home = std::env::var("HOME").unwrap_or_default();
    let root = if home.is_empty() { dir.to_string() } else { expand_home(dir, &home) };
    let wanted = |p: &str| if animated { wallpaper_is_video(p) } else { wallpaper_is_image(p) };
    let mut found = Vec::new();
    let mut folders = vec![(std::path::PathBuf::from(root), 0)];
    while let Some((folder, depth)) = folders.pop() {
        let Ok(entries) = std::fs::read_dir(&folder) else { continue };
        for entry in entries.flatten() {
            let Ok(kind) = entry.file_type() else { continue };
            let path = entry.path().to_string_lossy().into_owned();
            if kind.is_dir() && depth == 0 {
                folders.push((entry.path(), 1));
            } else if (kind.is_file() || (kind.is_symlink() && entry.path().is_file())) && wanted(&path) {
                found.push(path);
            }
        }
    }
    found.sort_by_key(|p| picker_key(p));
    found.truncate(limit);
    found
}

pub fn wallpaper_column_count(cfg: &Config, monitor: &str, slot: WallpaperSlot) -> i32 {
    let counts = match slot {
        WallpaperSlot::Still => &cfg.wallpaper_column_counts,
        WallpaperSlot::Animated => &cfg.wallpaper_animated_column_counts,
        WallpaperSlot::Lock => &cfg.lock_wallpaper_column_counts,
    };
    counts.get(monitor).copied().filter(|&c| c > 0).unwrap_or(1)
}

fn entry(map: &Columns, monitor: &str, column: i32) -> Option<String> {
    let column = usize::try_from(column).ok()?;
    map.get(monitor)?.get(column).cloned()
}

pub fn wallpaper_column_override(cfg: &Config, monitor: &str, column: i32, slot: WallpaperSlot) -> String {
    entry(column_map(cfg, slot), monitor, column).unwrap_or_default()
}

pub fn wallpaper_column_path(cfg: &Config, monitor: &str, column: i32, slot: WallpaperSlot) -> String {
    let path = wallpaper_column_override(cfg, monitor, column, slot);
    if !path.is_empty() {
        return path;
    }
    if cfg.default_wallpaper { cfg.wallpaper_path.clone() } else { String::new() }
}

pub fn wallpaper_fill_mode(cfg: &Config, monitor: &str, column: i32, slot: WallpaperSlot) -> String {
    entry(fill_mode_map(cfg, slot), monitor, column).filter(|m| !m.is_empty()).unwrap_or_else(|| "crop".into())
}

fn set_entry(map: &mut Columns, monitor: &str, column: i32, value: &str) {
    let Ok(column) = usize::try_from(column) else { return };
    let values = map.entry(monitor.into()).or_default();
    if column >= values.len() {
        values.resize(column + 1, String::new());
    }
    values[column] = value.into();
}

pub fn wallpaper_set_column(cfg: &mut Config, monitor: &str, column: i32, path: &str, slot: WallpaperSlot) {
    set_entry(column_map_mut(cfg, slot), monitor, column, path);
}

pub fn wallpaper_set_fill_mode(cfg: &mut Config, monitor: &str, column: i32, mode: &str, slot: WallpaperSlot) {
    set_entry(fill_mode_map_mut(cfg, slot), monitor, column, mode);
}

pub fn wallpaper_set_column_count(cfg: &mut Config, monitor: &str, count: i32, slot: WallpaperSlot) {
    count_map_mut(cfg, slot).insert(monitor.into(), count);
}

pub fn wallpaper_column_count_animated(cfg: &Config, monitor: &str, animated: bool) -> i32 {
    wallpaper_column_count(cfg, monitor, wallpaper_slot_of(animated))
}

pub fn wallpaper_column_override_animated(cfg: &Config, monitor: &str, column: i32, animated: bool) -> String {
    wallpaper_column_override(cfg, monitor, column, wallpaper_slot_of(animated))
}

pub fn wallpaper_column_path_animated(cfg: &Config, monitor: &str, column: i32, animated: bool) -> String {
    wallpaper_column_path(cfg, monitor, column, wallpaper_slot_of(animated))
}

pub fn wallpaper_fill_mode_animated(cfg: &Config, monitor: &str, column: i32, animated: bool) -> String {
    wallpaper_fill_mode(cfg, monitor, column, wallpaper_slot_of(animated))
}

pub fn wallpaper_set_column_animated(cfg: &mut Config, monitor: &str, column: i32, path: &str, animated: bool) {
    wallpaper_set_column(cfg, monitor, column, path, wallpaper_slot_of(animated));
}

pub fn wallpaper_set_fill_mode_animated(cfg: &mut Config, monitor: &str, column: i32, mode: &str, animated: bool) {
    wallpaper_set_fill_mode(cfg, monitor, column, mode, wallpaper_slot_of(animated));
}

pub fn wallpaper_set_column_count_animated(cfg: &mut Config, monitor: &str, count: i32, animated: bool) {
    wallpaper_set_column_count(cfg, monitor, count, wallpaper_slot_of(animated));
}

pub fn wallpaper_image_for(cfg: &Config, monitor: &str) -> String {
    wallpaper_column_path(cfg, monitor, 0, WallpaperSlot::Still)
}

pub fn wallpaper_set_image(cfg: &mut Config, monitor: &str, path: &str) {
    set_entry(&mut cfg.wallpaper_columns, monitor, 0, path);
}

/// Clears column 0; drops the output's entry when it has no other column.
pub fn wallpaper_clear_image(cfg: &mut Config, monitor: &str) {
    let Some(columns) = cfg.wallpaper_columns.get_mut(monitor) else { return };
    if columns.len() > 1 {
        columns[0].clear();
    } else {
        cfg.wallpaper_columns.remove(monitor);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn cols(v: &[&str]) -> Vec<String> {
        v.iter().map(|s| s.to_string()).collect()
    }

    fn base() -> Config {
        let mut cfg = Config::with_home("/home/t");
        cfg.wallpaper_path = "/global.png".into();
        cfg
    }

    #[test]
    fn wallpaper_resolve() {
        let mut cfg = base();
        cfg.wallpaper_columns.insert("DP-1".into(), cols(&["/dp1.png"]));
        cfg.wallpaper_fill_modes.insert("DP-1".into(), cols(&["fit", "tile"]));
        assert_eq!(wallpaper_column_path_animated(&cfg, "DP-1", 0, false), "/dp1.png");
        assert_eq!(wallpaper_column_path_animated(&cfg, "HDMI-1", 0, false), "/global.png");
        assert_eq!(wallpaper_fill_mode_animated(&cfg, "DP-1", 0, false), "fit");
        assert_eq!(wallpaper_fill_mode_animated(&cfg, "DP-1", 1, false), "tile");
        assert_eq!(wallpaper_fill_mode_animated(&cfg, "DP-1", 2, false), "crop");
        assert_eq!(wallpaper_fill_mode_animated(&cfg, "HDMI-1", 0, false), "crop");
        assert_eq!(wallpaper_column_path_animated(&cfg, "DP-1", 1, false), "/global.png");
        assert_eq!(wallpaper_column_path_animated(&cfg, "HDMI-1", 3, false), "/global.png");
        assert_eq!(wallpaper_column_count_animated(&cfg, "DP-1", false), 1);
        cfg.wallpaper_column_counts.insert("DP-1".into(), 2);
        assert_eq!(wallpaper_column_count_animated(&cfg, "DP-1", false), 2);

        cfg.default_wallpaper = false;
        assert!(wallpaper_column_path_animated(&cfg, "HDMI-1", 0, false).is_empty());
        assert!(wallpaper_column_path_animated(&cfg, "DP-1", 1, false).is_empty());
        assert_eq!(wallpaper_column_path_animated(&cfg, "DP-1", 0, false), "/dp1.png");
        assert!(wallpaper_column_override_animated(&cfg, "HDMI-1", 0, false).is_empty());
        assert_eq!(wallpaper_column_override_animated(&cfg, "DP-1", 0, false), "/dp1.png");
        cfg.default_wallpaper = true;
        assert!(wallpaper_column_override_animated(&cfg, "HDMI-1", 0, false).is_empty());

        cfg.wallpaper_animated_columns.insert("DP-1".into(), cols(&["/dp1.mp4"]));
        cfg.wallpaper_animated_fill_modes.insert("DP-1".into(), cols(&["fit"]));
        assert_eq!(wallpaper_column_path_animated(&cfg, "DP-1", 0, true), "/dp1.mp4");
        assert_eq!(wallpaper_column_path_animated(&cfg, "HDMI-1", 0, true), "/global.png");
        assert!(wallpaper_column_override_animated(&cfg, "HDMI-1", 0, true).is_empty());
        assert_eq!(wallpaper_column_override_animated(&cfg, "DP-1", 0, true), "/dp1.mp4");
        assert_eq!(wallpaper_fill_mode_animated(&cfg, "DP-1", 0, true), "fit");
        assert_eq!(wallpaper_fill_mode_animated(&cfg, "HDMI-1", 0, true), "crop");
        assert_eq!(wallpaper_column_count_animated(&cfg, "DP-1", true), 1);
        cfg.wallpaper_animated_column_counts.insert("DP-1".into(), 3);
        assert_eq!(wallpaper_column_count_animated(&cfg, "DP-1", true), 3);
        cfg.default_wallpaper = false;
        assert!(wallpaper_column_path_animated(&cfg, "HDMI-1", 0, true).is_empty());
        assert_eq!(wallpaper_column_path_animated(&cfg, "DP-1", 0, true), "/dp1.mp4");
    }

    #[test]
    fn wallpaper_lock_slot_rules() {
        use WallpaperSlot::{Lock, Still};
        let mut cfg = base();
        cfg.wallpaper_columns.insert("DP-1".into(), cols(&["/dp1.png"]));
        cfg.wallpaper_column_counts.insert("DP-1".into(), 2);
        cfg.wallpaper_fill_modes.insert("DP-1".into(), cols(&["fit"]));
        assert_eq!(wallpaper_column_count(&cfg, "DP-1", Lock), 1, "the lock slot ignores the desktop column count");
        assert!(wallpaper_column_override(&cfg, "DP-1", 0, Lock).is_empty(), "the lock slot ignores the desktop columns");
        assert_eq!(wallpaper_column_path(&cfg, "DP-1", 0, Lock), "/global.png", "an empty lock column falls back to the default wallpaper");
        assert_eq!(wallpaper_fill_mode(&cfg, "DP-1", 0, Lock), "crop", "the lock slot ignores the desktop fill modes");
        wallpaper_set_column(&mut cfg, "DP-1", 1, "/lock.png", Lock);
        wallpaper_set_fill_mode(&mut cfg, "DP-1", 1, "fit", Lock);
        wallpaper_set_column_count(&mut cfg, "DP-1", 3, Lock);
        assert!(
            wallpaper_column_override(&cfg, "DP-1", 1, Lock) == "/lock.png" && wallpaper_column_path(&cfg, "DP-1", 1, Lock) == "/lock.png" && wallpaper_column_override(&cfg, "DP-1", 0, Lock).is_empty(),
            "the lock slot stores its own columns"
        );
        assert!(wallpaper_fill_mode(&cfg, "DP-1", 1, Lock) == "fit" && wallpaper_column_count(&cfg, "DP-1", Lock) == 3);
        assert!(
            cfg.lock_wallpaper_columns.contains_key("DP-1")
                && wallpaper_column_override(&cfg, "DP-1", 0, Still) == "/dp1.png"
                && wallpaper_column_override(&cfg, "DP-1", 1, Still).is_empty()
                && wallpaper_column_count(&cfg, "DP-1", Still) == 2
                && wallpaper_fill_mode(&cfg, "DP-1", 1, Still) == "crop",
            "the desktop slot is untouched"
        );
        cfg.default_wallpaper = false;
        assert!(wallpaper_column_path(&cfg, "DP-1", 0, Lock).is_empty() && wallpaper_column_path(&cfg, "DP-1", 1, Lock) == "/lock.png");
        assert_eq!(wallpaper_lock_slot(&cfg), Still, "Auto mode draws the desktop slot");
        cfg.lock_wallpaper_custom = true;
        assert_eq!(wallpaper_lock_slot(&cfg), Lock, "Custom mode draws the lock slot");
        cfg.wallpaper_dir = "/pictures".into();
        cfg.wallpaper_animated_dir = "/videos".into();
        cfg.lock_wallpaper_dir = "/locks".into();
        assert!(wallpaper_slot_dir(&cfg, Still) == "/pictures" && wallpaper_slot_dir(&cfg, WallpaperSlot::Animated) == "/videos" && wallpaper_slot_dir(&cfg, Lock) == "/locks");
        assert!(wallpaper_slot_of(false) == Still && wallpaper_slot_of(true) == WallpaperSlot::Animated);
    }

    #[test]
    fn wallpaper_images() {
        let mut cfg = base();
        assert_eq!(wallpaper_image_for(&cfg, "eDP-1"), "/global.png", "an output without an image uses the global one");
        wallpaper_set_image(&mut cfg, "eDP-1", "/mine.png");
        assert!(wallpaper_image_for(&cfg, "eDP-1") == "/mine.png" && wallpaper_image_for(&cfg, "DP-1") == "/global.png");
        wallpaper_clear_image(&mut cfg, "eDP-1");
        assert!(wallpaper_image_for(&cfg, "eDP-1") == "/global.png" && !cfg.wallpaper_columns.contains_key("eDP-1"));
        cfg.wallpaper_columns.insert("DP-1".into(), cols(&["/a.png", "/b.png"]));
        wallpaper_clear_image(&mut cfg, "DP-1");
        assert!(wallpaper_column_override_animated(&cfg, "DP-1", 0, false).is_empty() && wallpaper_column_override_animated(&cfg, "DP-1", 1, false) == "/b.png");
    }

    #[test]
    fn wallpaper_listing() {
        assert!(wallpaper_is_image("/x/A.PNG") && wallpaper_is_image("b.jpeg") && !wallpaper_is_image("c.mp4") && !wallpaper_is_image("d.txt"));
        assert!(wallpaper_is_video("/x/a.MKV") && wallpaper_is_video("b.webm") && !wallpaper_is_video("c.png"));
        assert!(wallpaper_picker_less("/x/b.jpg", "/x/a.png") && wallpaper_picker_less("/x/a.png", "/x/B.png"));
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        std::fs::create_dir_all(root.join("sub/deep")).unwrap();
        for f in ["b.png", "a.jpg", "clip.mp4", "sub/c.png", "sub/deep/d.png"] {
            std::fs::write(root.join(f), "x").unwrap();
        }
        let root = root.to_str().unwrap();
        let images = wallpaper_list_files(root, false, 100);
        assert!(images.len() == 3 && images[0].ends_with("a.jpg") && images[1].ends_with("b.png") && images[2].ends_with("c.png"), "{images:?}");
        assert_eq!(wallpaper_list_files(root, false, 2).len(), 2, "the listing is capped");
        let videos = wallpaper_list_files(root, true, 100);
        assert!(videos.len() == 1 && videos[0].ends_with("clip.mp4"));
        assert!(wallpaper_list_files(&format!("{root}/missing"), false, 100).is_empty());
    }
}
