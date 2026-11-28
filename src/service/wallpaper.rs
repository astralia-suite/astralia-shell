use std::collections::BTreeMap;
use std::path::Path;

use crate::config::file::expand_home;
use crate::config::settings::{Config, Profiles, WallpaperProfile};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WallpaperSlot {
    Still,
    Lock,
}

type Columns = BTreeMap<String, Vec<String>>;

fn column_map(cfg: &Config, slot: WallpaperSlot) -> &Columns {
    match slot {
        WallpaperSlot::Still => &cfg.wallpaper_columns,
        WallpaperSlot::Lock => &cfg.lock_wallpaper_columns,
    }
}

fn column_map_mut(cfg: &mut Config, slot: WallpaperSlot) -> &mut Columns {
    match slot {
        WallpaperSlot::Still => &mut cfg.wallpaper_columns,
        WallpaperSlot::Lock => &mut cfg.lock_wallpaper_columns,
    }
}

fn fill_mode_map(cfg: &Config, slot: WallpaperSlot) -> &Columns {
    match slot {
        WallpaperSlot::Still => &cfg.wallpaper_fill_modes,
        WallpaperSlot::Lock => &cfg.lock_wallpaper_fill_modes,
    }
}

fn fill_mode_map_mut(cfg: &mut Config, slot: WallpaperSlot) -> &mut Columns {
    match slot {
        WallpaperSlot::Still => &mut cfg.wallpaper_fill_modes,
        WallpaperSlot::Lock => &mut cfg.lock_wallpaper_fill_modes,
    }
}

fn count_map_mut(cfg: &mut Config, slot: WallpaperSlot) -> &mut BTreeMap<String, i32> {
    match slot {
        WallpaperSlot::Still => &mut cfg.wallpaper_column_counts,
        WallpaperSlot::Lock => &mut cfg.lock_wallpaper_column_counts,
    }
}

#[cfg(test)]
pub fn wallpaper_lock_slot(cfg: &Config) -> WallpaperSlot {
    if cfg.lock_wallpaper_custom { WallpaperSlot::Lock } else { WallpaperSlot::Still }
}

pub fn wallpaper_slot_dir(cfg: &Config, slot: WallpaperSlot) -> &str {
    match slot {
        WallpaperSlot::Still => &cfg.wallpaper_dir,
        WallpaperSlot::Lock => &cfg.lock_wallpaper_dir,
    }
}

fn extension(path: &str) -> String {
    Path::new(path).extension().map(|e| format!(".{}", e.to_string_lossy().to_lowercase())).unwrap_or_default()
}

pub fn wallpaper_is_image(path: &str) -> bool {
    matches!(extension(path).as_str(), ".png" | ".jpg" | ".jpeg" | ".jfif" | ".bmp" | ".svg")
}

/// By extension, then by file name, ignoring case.
fn picker_key(path: &str) -> (String, String) {
    (extension(path), Path::new(path).file_name().map(|n| n.to_string_lossy().to_lowercase()).unwrap_or_default())
}

#[cfg(test)]
pub fn wallpaper_picker_less(a: &str, b: &str) -> bool {
    picker_key(a) < picker_key(b)
}

/// Images directly in `dir` (subfolders are not searched), in picker order, at most `limit`.
pub fn wallpaper_list_files(dir: &str, limit: usize) -> Vec<String> {
    let home = crate::base::paths::home();
    let root = if home.is_empty() { dir.to_string() } else { expand_home(dir, &home) };
    let Ok(entries) = std::fs::read_dir(root) else { return Vec::new() };
    let mut found: Vec<String> = entries
        .flatten()
        .filter(|entry| entry.path().is_file())
        .map(|entry| entry.path().to_string_lossy().into_owned())
        .filter(|path| wallpaper_is_image(path))
        .collect();
    found.sort_by_key(|p| picker_key(p));
    found.truncate(limit);
    found
}

pub fn wallpaper_column_count(cfg: &Config, monitor: &str, slot: WallpaperSlot) -> i32 {
    let counts = match slot {
        WallpaperSlot::Still => &cfg.wallpaper_column_counts,
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

pub fn wallpaper_profiles(cfg: &Config, slot: WallpaperSlot) -> &Profiles {
    match slot {
        WallpaperSlot::Still => &cfg.wallpaper_profiles,
        WallpaperSlot::Lock => &cfg.lock_wallpaper_profiles,
    }
}

fn profiles_mut(cfg: &mut Config, slot: WallpaperSlot) -> &mut Profiles {
    match slot {
        WallpaperSlot::Still => &mut cfg.wallpaper_profiles,
        WallpaperSlot::Lock => &mut cfg.lock_wallpaper_profiles,
    }
}

/// The slot's live folder, columns, counts and fill modes.
fn live_parts(cfg: &mut Config, slot: WallpaperSlot) -> (&mut String, &mut Columns, &mut BTreeMap<String, i32>, &mut Columns) {
    match slot {
        WallpaperSlot::Still => (&mut cfg.wallpaper_dir, &mut cfg.wallpaper_columns, &mut cfg.wallpaper_column_counts, &mut cfg.wallpaper_fill_modes),
        WallpaperSlot::Lock => {
            (&mut cfg.lock_wallpaper_dir, &mut cfg.lock_wallpaper_columns, &mut cfg.lock_wallpaper_column_counts, &mut cfg.lock_wallpaper_fill_modes)
        }
    }
}

/// Saves the slot's live settings as `name`, replacing a profile of that name; a blank name is ignored.
pub fn wallpaper_profile_save(cfg: &mut Config, slot: WallpaperSlot, name: &str) {
    let name = name.trim();
    if name.is_empty() {
        return;
    }
    let (dir, columns, column_counts, fill_modes) = live_parts(cfg, slot);
    let profile = WallpaperProfile { dir: dir.clone(), columns: columns.clone(), column_counts: column_counts.clone(), fill_modes: fill_modes.clone() };
    profiles_mut(cfg, slot).insert(name.into(), profile);
}

/// Makes the profile `name` the slot's live settings; false when there is none.
pub fn wallpaper_profile_load(cfg: &mut Config, slot: WallpaperSlot, name: &str) -> bool {
    let Some(p) = wallpaper_profiles(cfg, slot).get(name).cloned() else { return false };
    let (dir, columns, column_counts, fill_modes) = live_parts(cfg, slot);
    (*dir, *columns, *column_counts, *fill_modes) = (p.dir, p.columns, p.column_counts, p.fill_modes);
    true
}

pub fn wallpaper_profile_delete(cfg: &mut Config, slot: WallpaperSlot, name: &str) {
    profiles_mut(cfg, slot).remove(name);
}

pub fn wallpaper_image_for(cfg: &Config, monitor: &str) -> String {
    wallpaper_column_path(cfg, monitor, 0, WallpaperSlot::Still)
}

#[cfg(test)]
pub fn wallpaper_set_image(cfg: &mut Config, monitor: &str, path: &str) {
    set_entry(&mut cfg.wallpaper_columns, monitor, 0, path);
}

/// Clears column 0; drops the output's entry when it has no other column.
#[cfg(test)]
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
        use WallpaperSlot::Still;
        assert_eq!(wallpaper_column_path(&cfg, "DP-1", 0, Still), "/dp1.png");
        assert_eq!(wallpaper_column_path(&cfg, "HDMI-1", 0, Still), "/global.png");
        assert_eq!(wallpaper_fill_mode(&cfg, "DP-1", 0, Still), "fit");
        assert_eq!(wallpaper_fill_mode(&cfg, "DP-1", 1, Still), "tile");
        assert_eq!(wallpaper_fill_mode(&cfg, "DP-1", 2, Still), "crop");
        assert_eq!(wallpaper_fill_mode(&cfg, "HDMI-1", 0, Still), "crop");
        assert_eq!(wallpaper_column_path(&cfg, "DP-1", 1, Still), "/global.png");
        assert_eq!(wallpaper_column_path(&cfg, "HDMI-1", 3, Still), "/global.png");
        assert_eq!(wallpaper_column_count(&cfg, "DP-1", Still), 1);
        cfg.wallpaper_column_counts.insert("DP-1".into(), 2);
        assert_eq!(wallpaper_column_count(&cfg, "DP-1", Still), 2);

        cfg.default_wallpaper = false;
        assert!(wallpaper_column_path(&cfg, "HDMI-1", 0, Still).is_empty());
        assert!(wallpaper_column_path(&cfg, "DP-1", 1, Still).is_empty());
        assert_eq!(wallpaper_column_path(&cfg, "DP-1", 0, Still), "/dp1.png");
        assert!(wallpaper_column_override(&cfg, "HDMI-1", 0, Still).is_empty());
        assert_eq!(wallpaper_column_override(&cfg, "DP-1", 0, Still), "/dp1.png");
        cfg.default_wallpaper = true;
        assert!(wallpaper_column_override(&cfg, "HDMI-1", 0, Still).is_empty());
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
        cfg.lock_wallpaper_dir = "/locks".into();
        assert!(wallpaper_slot_dir(&cfg, Still) == "/pictures" && wallpaper_slot_dir(&cfg, Lock) == "/locks");
    }

    #[test]
    fn wallpaper_profile_rules() {
        use WallpaperSlot::{Lock, Still};
        let mut cfg = base();
        cfg.wallpaper_dir = "/night".into();
        wallpaper_set_column(&mut cfg, "DP-1", 0, "/night/a.png", Still);
        wallpaper_set_column_count(&mut cfg, "DP-1", 2, Still);
        wallpaper_set_fill_mode(&mut cfg, "DP-1", 0, "fit", Still);
        wallpaper_profile_save(&mut cfg, Still, "  Night ");
        wallpaper_profile_save(&mut cfg, Still, "   ");
        assert!(wallpaper_profiles(&cfg, Still).keys().eq(["Night"]), "the name is trimmed and a blank one ignored");
        assert!(wallpaper_profiles(&cfg, Lock).is_empty(), "the lock slot keeps its own list");

        let saved = cfg.clone();
        cfg.wallpaper_dir = "/day".into();
        wallpaper_set_column(&mut cfg, "DP-1", 0, "/day/b.png", Still);
        wallpaper_set_column_count(&mut cfg, "DP-1", 1, Still);
        wallpaper_set_fill_mode(&mut cfg, "DP-1", 0, "crop", Still);
        wallpaper_set_column(&mut cfg, "DP-1", 0, "/lock.png", Lock);
        assert!(wallpaper_profile_load(&mut cfg, Still, "Night"));
        assert!(
            cfg.wallpaper_dir == saved.wallpaper_dir
                && cfg.wallpaper_columns == saved.wallpaper_columns
                && cfg.wallpaper_column_counts == saved.wallpaper_column_counts
                && cfg.wallpaper_fill_modes == saved.wallpaper_fill_modes,
            "loading restores the desktop slot"
        );
        assert_eq!(wallpaper_column_override(&cfg, "DP-1", 0, Lock), "/lock.png", "loading a desktop profile leaves the lock slot alone");
        assert!(!wallpaper_profile_load(&mut cfg, Lock, "Night"), "a desktop profile is not a lock profile");
        wallpaper_profile_delete(&mut cfg, Still, "Night");
        assert!(wallpaper_profiles(&cfg, Still).is_empty());
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
        assert!(wallpaper_column_override(&cfg, "DP-1", 0, WallpaperSlot::Still).is_empty() && wallpaper_column_override(&cfg, "DP-1", 1, WallpaperSlot::Still) == "/b.png");
    }

    #[test]
    fn wallpaper_listing() {
        assert!(wallpaper_is_image("/x/A.PNG") && wallpaper_is_image("b.jpeg") && !wallpaper_is_image("c.mp4") && !wallpaper_is_image("d.txt"));
        assert!(wallpaper_picker_less("/x/b.jpg", "/x/a.png") && wallpaper_picker_less("/x/a.png", "/x/B.png"));
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        std::fs::create_dir_all(root.join("sub/deep")).unwrap();
        for f in ["b.png", "a.jpg", "clip.mp4", "sub/c.png", "sub/deep/d.png"] {
            std::fs::write(root.join(f), "x").unwrap();
        }
        let root = root.to_str().unwrap();
        let images = wallpaper_list_files(root, 100);
        assert!(images.len() == 2 && images[0].ends_with("a.jpg") && images[1].ends_with("b.png"), "subfolders are not searched: {images:?}");
        assert_eq!(wallpaper_list_files(root, 2).len(), 2, "the listing is capped");
        assert!(wallpaper_list_files(&format!("{root}/missing"), 100).is_empty());
    }
}
