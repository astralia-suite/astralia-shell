//! Icon file lookup: themed names, desktop-entry window classes and pixmaps.

use std::cell::{OnceCell, RefCell};
use std::collections::HashMap;
use std::path::Path;
use std::sync::{LazyLock, Mutex};

const FALLBACK_THEMES: [&str; 4] = ["Adwaita", "AdwaitaLegacy", "breeze", "breeze-dark"];
const MISSING_ICON: &str = "application-x-executable";

/// The active theme first (unless it is `hicolor`), then the common fallbacks, `hicolor` last.
pub fn icon_theme_order(active: &str) -> Vec<String> {
    let mut themes = Vec::new();
    if !active.is_empty() && active != "hicolor" {
        themes.push(active.to_string());
    }
    for theme in FALLBACK_THEMES {
        if !themes.iter().any(|t| t == theme) {
            themes.push(theme.to_string());
        }
    }
    themes.push("hicolor".to_string());
    themes
}

/// An absolute PNG path is used as is; anything else needs a lookup.
pub fn icon_direct_path(field: &str) -> String {
    if field.starts_with('/') && field.ends_with(".png") { field.to_string() } else { String::new() }
}

/// The file for a desktop-entry `Icon` value, or empty.
pub fn app_icon_path(field: &str) -> String {
    system(|lookup| lookup.app_icon_path(field))
}

/// The icon of the application whose window has `class`, else the generic executable icon.
pub fn window_icon_path(class: &str) -> String {
    system(|lookup| lookup.window_icon_path(class))
}

fn system<T>(f: impl FnOnce(&IconLookup) -> T) -> T {
    static SYSTEM: LazyLock<Mutex<IconLookup>> = LazyLock::new(|| Mutex::new(IconLookup::system()));
    let guard = SYSTEM.lock().unwrap_or_else(|e| e.into_inner());
    f(&guard)
}

#[derive(Default)]
struct ThemeIndex {
    png: HashMap<String, String>,
    svg: HashMap<String, String>,
}

/// Lookups over explicit directories; every index is built on first use and kept.
pub struct IconLookup {
    icon_bases: Vec<String>,
    app_dirs: Vec<String>,
    pixmaps: String,
    order: Vec<String>,
    themes: RefCell<HashMap<String, ThemeIndex>>,
    classes: OnceCell<HashMap<String, String>>,
    resolved: RefCell<HashMap<String, String>>,
}

fn env(name: &str) -> String {
    std::env::var(name).unwrap_or_default()
}

fn active_gtk_theme(home: &str) -> String {
    if home.is_empty() {
        return String::new();
    }
    let text = std::fs::read_to_string(format!("{home}/.config/gtk-3.0/settings.ini")).unwrap_or_default();
    text.lines()
        .find_map(|line| line.strip_prefix("gtk-icon-theme-name="))
        .map(|v| v.trim_end_matches(['\r', ' ']).to_string())
        .unwrap_or_default()
}

/// Every regular file under `root`, without following directory links.
fn walk(root: &Path, visit: &mut dyn FnMut(&Path)) {
    let Ok(entries) = std::fs::read_dir(root) else { return };
    for entry in entries.flatten() {
        let path = entry.path();
        match entry.file_type() {
            Ok(t) if t.is_dir() => walk(&path, visit),
            Ok(_) if path.is_file() => visit(&path),
            _ => {}
        }
    }
}

fn stem(path: &Path) -> String {
    path.file_stem().map(|s| s.to_string_lossy().into_owned()).unwrap_or_default()
}

/// The `Icon` of a desktop file under the stem and `StartupWMClass`, both lowercased; the first entry wins.
fn index_desktop_file(path: &Path, index: &mut HashMap<String, String>) {
    let text = std::fs::read_to_string(path).unwrap_or_default();
    let (mut icon, mut class, mut in_section) = (String::new(), String::new(), false);
    for line in text.lines() {
        let line = line.strip_suffix('\r').unwrap_or(line);
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        if line.starts_with('[') {
            if in_section {
                break;
            }
            in_section = line == "[Desktop Entry]";
            continue;
        }
        let Some((key, value)) = line.split_once('=') else { continue };
        if !in_section {
            continue;
        }
        match key {
            "Icon" => icon = value.to_string(),
            "StartupWMClass" => class = value.to_string(),
            _ => {}
        }
    }
    if icon.is_empty() {
        return;
    }
    index.entry(stem(path).to_lowercase()).or_insert_with(|| icon.clone());
    if !class.is_empty() {
        index.entry(class.to_lowercase()).or_insert(icon);
    }
}

impl IconLookup {
    pub fn new(icon_bases: Vec<String>, app_dirs: Vec<String>, pixmaps: String, active_theme: &str) -> IconLookup {
        IconLookup {
            icon_bases,
            app_dirs,
            pixmaps,
            order: icon_theme_order(active_theme),
            themes: RefCell::default(),
            classes: OnceCell::new(),
            resolved: RefCell::default(),
        }
    }

    /// `~/.local/share/icons` and `/usr/share/icons`, the XDG application directories, the GTK icon theme.
    fn system() -> IconLookup {
        let home = env("HOME");
        let mut bases = Vec::new();
        let mut apps = Vec::new();
        let data_home = env("XDG_DATA_HOME");
        if !home.is_empty() {
            bases.push(format!("{home}/.local/share/icons"));
        }
        bases.push("/usr/share/icons".to_string());
        if !data_home.is_empty() {
            apps.push(format!("{data_home}/applications"));
        } else if !home.is_empty() {
            apps.push(format!("{home}/.local/share/applications"));
        }
        let data_dirs = env("XDG_DATA_DIRS");
        let data_dirs = if data_dirs.is_empty() { "/usr/local/share:/usr/share".to_string() } else { data_dirs };
        apps.extend(data_dirs.split(':').filter(|d| !d.is_empty()).map(|d| format!("{d}/applications")));
        IconLookup::new(bases, apps, "/usr/share/pixmaps".to_string(), &active_gtk_theme(&home))
    }

    fn find_in_theme(&self, theme: &str, name: &str, svg: bool) -> Option<String> {
        let mut themes = self.themes.borrow_mut();
        let index = themes.entry(theme.to_string()).or_insert_with(|| {
            let mut index = ThemeIndex::default();
            for base in &self.icon_bases {
                walk(&Path::new(base).join(theme), &mut |path| {
                    let map = match path.extension().and_then(|e| e.to_str()) {
                        Some("png") => &mut index.png,
                        Some("svg") => &mut index.svg,
                        _ => return,
                    };
                    map.entry(stem(path)).or_insert_with(|| path.to_string_lossy().into_owned());
                });
            }
            index
        });
        if svg { index.svg.get(name).cloned() } else { index.png.get(name).cloned() }
    }

    /// An absolute path if it exists; a name as a PNG in any theme, then an SVG, then a pixmap.
    pub fn app_icon_path(&self, field: &str) -> String {
        if field.is_empty() {
            return String::new();
        }
        if field.starts_with('/') {
            return if Path::new(field).exists() { field.to_string() } else { String::new() };
        }
        if let Some(found) = self.resolved.borrow().get(field) {
            return found.clone();
        }
        let found = [false, true]
            .iter()
            .find_map(|&svg| self.order.iter().find_map(|theme| self.find_in_theme(theme, field, svg)))
            .or_else(|| {
                [".png", ".svg"].iter().map(|ext| format!("{}/{field}{ext}", self.pixmaps)).find(|p| Path::new(p).exists())
            })
            .unwrap_or_default();
        self.resolved.borrow_mut().insert(field.to_string(), found.clone());
        found
    }

    pub fn window_icon_path(&self, class: &str) -> String {
        if !class.is_empty() {
            let classes = self.classes.get_or_init(|| {
                let mut index = HashMap::new();
                for dir in &self.app_dirs {
                    walk(Path::new(dir), &mut |path| {
                        if path.extension().is_some_and(|e| e == "desktop") {
                            index_desktop_file(path, &mut index);
                        }
                    });
                }
                index
            });
            if let Some(icon) = classes.get(&class.to_lowercase()) {
                let path = self.app_icon_path(icon);
                if !path.is_empty() {
                    return path;
                }
            }
            let path = self.app_icon_path(class);
            if !path.is_empty() {
                return path;
            }
        }
        self.app_icon_path(MISSING_ICON)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn touch(path: &std::path::Path) {
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, b"x").unwrap();
    }

    #[test]
    fn icon_lookup() {
        assert_eq!(icon_theme_order("Papirus").first().map(String::as_str), Some("Papirus"));
        assert_eq!(icon_theme_order("").last().map(String::as_str), Some("hicolor"));
        assert_ne!(icon_theme_order("hicolor").first().map(String::as_str), Some("hicolor"));
        assert_eq!(icon_theme_order("breeze"), ["breeze", "Adwaita", "AdwaitaLegacy", "breeze-dark", "hicolor"]);
        assert_eq!(icon_direct_path("/a/b.png"), "/a/b.png");
        assert_eq!(icon_direct_path("/a/b.svg"), "");
        assert_eq!(icon_direct_path("name"), "");
        assert_eq!(app_icon_path(""), "");

        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        let p = |rel: &str| root.join(rel).to_str().unwrap().to_string();
        touch(&root.join("home/icons/Mine/48x48/apps/foo.png"));
        touch(&root.join("usr/icons/hicolor/48x48/apps/foo.png"));
        touch(&root.join("usr/icons/hicolor/scalable/apps/bar.svg"));
        touch(&root.join("usr/icons/Adwaita/scalable/apps/bar.png"));
        touch(&root.join("usr/icons/hicolor/scalable/apps/application-x-executable.svg"));
        touch(&root.join("pixmaps/baz.png"));
        std::fs::create_dir_all(root.join("apps/sub")).unwrap();
        std::fs::write(root.join("apps/sub/Some.App.desktop"), "# c\n[Desktop Entry]\nName=Some\nIcon=foo\r\nStartupWMClass=SomeClass\n[Desktop Action x]\nIcon=bar\n").unwrap();
        std::fs::write(root.join("apps/noicon.desktop"), "[Desktop Entry]\nName=None\n").unwrap();

        let lookup = IconLookup::new(vec![p("home/icons"), p("usr/icons")], vec![p("apps")], p("pixmaps"), "Mine");
        assert_eq!(lookup.app_icon_path("foo"), p("home/icons/Mine/48x48/apps/foo.png"), "active theme first");
        assert_eq!(lookup.app_icon_path("bar"), p("usr/icons/Adwaita/scalable/apps/bar.png"), "any theme's PNG before an SVG");
        assert_eq!(lookup.app_icon_path("baz"), p("pixmaps/baz.png"));
        assert_eq!(lookup.app_icon_path("nothing"), "");
        assert_eq!(lookup.app_icon_path(&p("pixmaps/baz.png")), p("pixmaps/baz.png"));
        assert_eq!(lookup.app_icon_path(&p("pixmaps/gone.png")), "");
        assert_eq!(lookup.window_icon_path("someclass"), p("home/icons/Mine/48x48/apps/foo.png"), "StartupWMClass, any case");
        assert_eq!(lookup.window_icon_path("some.app"), p("home/icons/Mine/48x48/apps/foo.png"), "desktop file stem");
        assert_eq!(lookup.window_icon_path("baz"), p("pixmaps/baz.png"), "the class as an icon name");
        assert_eq!(lookup.window_icon_path("unknown"), p("usr/icons/hicolor/scalable/apps/application-x-executable.svg"));
        assert_eq!(lookup.window_icon_path(""), p("usr/icons/hicolor/scalable/apps/application-x-executable.svg"));
    }
}
