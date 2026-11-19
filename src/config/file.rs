pub fn config_file_path(config_home: &str, home: &str, relative: &str) -> String {
    if config_home.is_empty() { format!("{home}/.config/{relative}") } else { format!("{config_home}/{relative}") }
}

pub fn user_config_path(relative: &str) -> String {
    let var = |name| std::env::var(name).unwrap_or_default();
    config_file_path(&var("XDG_CONFIG_HOME"), &var("HOME"), relative)
}

pub fn expand_home(path: &str, home: &str) -> String {
    if path == "~" {
        return home.to_string();
    }
    match path.strip_prefix('~') {
        Some(rest) if rest.starts_with('/') => format!("{home}{rest}"),
        _ => path.to_string(),
    }
}

/// `path` with a leading `home` written as `~`; unchanged when `home` is empty.
pub fn collapse_home(path: &str, home: &str) -> String {
    match path.strip_prefix(home) {
        Some("") if !home.is_empty() => "~".to_string(),
        Some(rest) if !home.is_empty() && rest.starts_with('/') => format!("~{rest}"),
        _ => path.to_string(),
    }
}

pub fn read_text_file(path: &str) -> Option<String> {
    std::fs::read(path).ok().map(|bytes| String::from_utf8_lossy(&bytes).into_owned())
}

/// Writes `text` to `path.tmp` and renames it over `path`, so readers never see a partial file.
pub fn write_text_file(path: &str, text: &str) -> bool {
    let target = std::path::Path::new(path);
    if let Some(dir) = target.parent() {
        let _ = std::fs::create_dir_all(dir);
    }
    let temporary = format!("{path}.tmp");
    if let Err(e) = std::fs::write(&temporary, text) {
        ::log::error!("cannot write {temporary}: {e}");
        return false;
    }
    if let Err(e) = std::fs::rename(&temporary, target) {
        ::log::error!("cannot replace {path}: {e}");
        return false;
    }
    true
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn config_file_paths() {
        assert_eq!(config_file_path("/cfg", "/home/u", "astralia/wallpaper.conf"), "/cfg/astralia/wallpaper.conf", "XDG_CONFIG_HOME wins");
        assert_eq!(
            config_file_path("", "/home/u", "astralia/wallpaper.conf"),
            "/home/u/.config/astralia/wallpaper.conf",
            "empty XDG_CONFIG_HOME falls back to ~/.config"
        );
        assert_eq!(expand_home("~/a.png", "/home/u"), "/home/u/a.png", "~/ expands");
        assert_eq!(expand_home("/a/~b.png", "/home/u"), "/a/~b.png", "inner ~ is kept");
        assert_eq!(expand_home("~", "/home/u"), "/home/u");
        assert_eq!(read_text_file("/nonexistent/astralia"), None);
        assert_eq!(collapse_home("/home/u/a.png", "/home/u"), "~/a.png");
        assert_eq!(collapse_home("/home/u", "/home/u"), "~");
        assert_eq!(collapse_home("/home/user/a.png", "/home/u"), "/home/user/a.png", "a sibling prefix is kept");
        assert_eq!(collapse_home("/home/u/a.png", ""), "/home/u/a.png", "no home, no collapse");
    }

    #[test]
    fn write_text_file_replaces() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("a/b/config.json");
        let path = path.to_str().unwrap();
        assert!(write_text_file(path, "one"), "parent directories are created");
        assert!(write_text_file(path, "two"));
        assert_eq!(read_text_file(path).as_deref(), Some("two"));
        assert!(!std::path::Path::new(&format!("{path}.tmp")).exists(), "the temporary file is renamed away");
    }
}
