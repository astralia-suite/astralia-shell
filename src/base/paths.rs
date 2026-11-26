use std::env;
use std::path::PathBuf;

/// Every runtime name is `<PREFIX>[-<id>]<ext>`.
pub const PREFIX: &str = "astralia";

fn var(name: &str) -> String {
    env::var(name).unwrap_or_default()
}

/// `$HOME`, empty when unset.
pub fn home() -> String {
    var("HOME")
}

pub fn runtime_path_in(session: &str, runtime_dir: &str, suffix: &str) -> PathBuf {
    let dir = if runtime_dir.is_empty() { "/tmp" } else { runtime_dir };
    let id: String = session.chars().filter(|&c| c != ':').map(|c| if c == '/' { '_' } else { c }).collect();
    let name = if id.is_empty() { format!("{PREFIX}{suffix}") } else { format!("{PREFIX}-{id}{suffix}") };
    PathBuf::from(dir).join(name)
}

pub fn runtime_path(suffix: &str) -> PathBuf {
    let mut session = var("WAYLAND_DISPLAY");
    if session.is_empty() {
        session = var("DISPLAY");
    }
    runtime_path_in(&session, &var("XDG_RUNTIME_DIR"), suffix)
}

pub fn log_path_in(state_home: &str, home: &str) -> PathBuf {
    let base = if state_home.is_empty() { PathBuf::from(home).join(".local/state") } else { PathBuf::from(state_home) };
    base.join("astralia").join(format!("{PREFIX}.log"))
}

pub fn log_path() -> PathBuf {
    log_path_in(&var("XDG_STATE_HOME"), &var("HOME"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    #[test]
    fn runtime_path_basic() {
        assert_eq!(runtime_path_in("wayland-1", "/run/user/1000", ".sock"), PathBuf::from("/run/user/1000/astralia-wayland-1.sock"));
        assert_eq!(runtime_path_in(":0", "", ".lock"), PathBuf::from("/tmp/astralia-0.lock"));
        assert_eq!(runtime_path_in("", "/run", ".sock"), PathBuf::from("/run/astralia.sock"));
    }

    #[test]
    fn runtime_path_maps_slashes() {
        assert_eq!(
            runtime_path_in("/run/user/1000/wayland-1", "/run/user/1000", ".sock"),
            PathBuf::from("/run/user/1000/astralia-_run_user_1000_wayland-1.sock")
        );
    }

    #[test]
    fn log_path() {
        assert_eq!(log_path_in("/state", "/home/k"), PathBuf::from("/state/astralia/astralia.log"));
        assert_eq!(log_path_in("", "/home/k"), PathBuf::from("/home/k/.local/state/astralia/astralia.log"));
    }
}
