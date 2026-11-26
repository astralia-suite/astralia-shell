use std::env;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SessionKind {
    Wayland,
    None,
}

#[derive(Debug, Default, Clone)]
pub struct SessionEnv {
    pub override_name: String,
    pub hyprland_signature: String,
    pub session_type: String,
    pub wayland_display: String,
}

impl SessionEnv {
    pub fn from_env() -> SessionEnv {
        let var = |name| env::var(name).unwrap_or_default();
        SessionEnv {
            override_name: var("ASTRALIA_BACKEND"),
            hyprland_signature: var("HYPRLAND_INSTANCE_SIGNATURE"),
            session_type: var("XDG_SESSION_TYPE"),
            wayland_display: var("WAYLAND_DISPLAY"),
        }
    }
}

pub fn detect(env: &SessionEnv) -> SessionKind {
    let wayland = env.override_name == "wayland"
        || !env.hyprland_signature.is_empty()
        || env.session_type == "wayland"
        || !env.wayland_display.is_empty();
    if wayland { SessionKind::Wayland } else { SessionKind::None }
}

/// Only `ASTRALIA_START_LOCKED=1` locks at startup.
pub fn start_locked(value: &str) -> bool {
    value == "1"
}

/// The file that marks this login's startup lock as done, so a restart does not lock again.
pub fn start_locked_ack_path(uid: u32) -> String {
    format!("/tmp/astralia-{uid}")
}

/// `ASTRALIA_START_LOCKED=1` and this login has not locked at startup yet.
pub fn start_locked_pending() -> bool {
    // SAFETY: `getuid` has no preconditions.
    let uid = unsafe { libc::getuid() };
    start_locked_pending_at(&env::var("ASTRALIA_START_LOCKED").unwrap_or_default(), std::path::Path::new(&start_locked_ack_path(uid)))
}

fn start_locked_pending_at(value: &str, ack: &std::path::Path) -> bool {
    start_locked(value) && !ack.exists()
}

pub fn start_locked_ack() {
    // SAFETY: `getuid` has no preconditions.
    let path = start_locked_ack_path(unsafe { libc::getuid() });
    if let Err(e) = std::fs::File::create(&path) {
        ::log::warn!("lock: cannot write {path}: {e}");
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn start_locked_checks() {
        assert!(super::start_locked("1") && !super::start_locked("") && !super::start_locked("0") && !super::start_locked("true"), "only ASTRALIA_START_LOCKED=1 locks at startup");
        assert_eq!(super::start_locked_ack_path(1000), "/tmp/astralia-1000", "the startup lock acknowledgement lives in /tmp per user");
    }

    #[test]
    fn start_locked_pending_once() {
        let dir = tempfile::tempdir().unwrap();
        let ack = dir.path().join("ack");
        assert!(super::start_locked_pending_at("1", &ack), "pending with the variable set and no ack file");
        assert!(!super::start_locked_pending_at("", &ack), "not pending without the variable");
        std::fs::write(&ack, "").unwrap();
        assert!(!super::start_locked_pending_at("1", &ack), "not pending once the file exists");
    }

    use super::*;

    #[test]
    fn detect_order() {
        let mut env = SessionEnv::default();
        assert_eq!(detect(&env), SessionKind::None, "an empty environment has no session");
        env.override_name = "x11".into();
        assert_eq!(detect(&env), SessionKind::None, "an unknown override is ignored");
        env.wayland_display = "wayland-1".into();
        assert_eq!(detect(&env), SessionKind::Wayland, "WAYLAND_DISPLAY means wayland");
        env.wayland_display.clear();
        env.session_type = "wayland".into();
        assert_eq!(detect(&env), SessionKind::Wayland, "XDG_SESSION_TYPE=wayland means wayland");
        env.session_type.clear();
        env.hyprland_signature = "abc_123".into();
        assert_eq!(detect(&env), SessionKind::Wayland, "the Hyprland signature means wayland");
        env.hyprland_signature.clear();
        env.override_name = "wayland".into();
        assert_eq!(detect(&env), SessionKind::Wayland, "the override can force wayland");
    }
}
