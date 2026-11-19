use std::env;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SessionKind {
    Wayland,
    X11,
    None,
}

#[derive(Debug, Default, Clone)]
pub struct SessionEnv {
    pub override_name: String,
    pub hyprland_signature: String,
    pub i3_socket: String,
    pub session_type: String,
    pub wayland_display: String,
    pub display: String,
}

impl SessionEnv {
    pub fn from_env() -> SessionEnv {
        let var = |name| env::var(name).unwrap_or_default();
        SessionEnv {
            override_name: var("ASTRALIA_BACKEND"),
            hyprland_signature: var("HYPRLAND_INSTANCE_SIGNATURE"),
            i3_socket: var("I3SOCK"),
            session_type: var("XDG_SESSION_TYPE"),
            wayland_display: var("WAYLAND_DISPLAY"),
            display: var("DISPLAY"),
        }
    }
}

pub fn detect(env: &SessionEnv) -> SessionKind {
    match env.override_name.as_str() {
        "wayland" => return SessionKind::Wayland,
        "x11" => return SessionKind::X11,
        _ => {}
    }
    if !env.hyprland_signature.is_empty() {
        return SessionKind::Wayland;
    }
    if !env.i3_socket.is_empty() {
        return SessionKind::X11;
    }
    match env.session_type.as_str() {
        "wayland" => return SessionKind::Wayland,
        "x11" => return SessionKind::X11,
        _ => {}
    }
    if !env.wayland_display.is_empty() {
        return SessionKind::Wayland;
    }
    if !env.display.is_empty() {
        return SessionKind::X11;
    }
    SessionKind::None
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
        env.display = ":0".into();
        assert_eq!(detect(&env), SessionKind::X11, "DISPLAY alone means x11");
        env.wayland_display = "wayland-1".into();
        assert_eq!(detect(&env), SessionKind::Wayland, "WAYLAND_DISPLAY wins over DISPLAY");
        env.session_type = "x11".into();
        assert_eq!(detect(&env), SessionKind::X11, "XDG_SESSION_TYPE wins over the display variables");
        env.i3_socket = "/run/user/1000/i3/ipc-socket.1".into();
        env.session_type = "wayland".into();
        assert_eq!(detect(&env), SessionKind::X11, "I3SOCK wins over XDG_SESSION_TYPE");
        env.hyprland_signature = "abc_123".into();
        assert_eq!(detect(&env), SessionKind::Wayland, "the Hyprland signature wins over I3SOCK");
        env.override_name = "x11".into();
        assert_eq!(detect(&env), SessionKind::X11, "the override wins over everything");
        env.override_name = "wayland".into();
        env.hyprland_signature.clear();
        assert_eq!(detect(&env), SessionKind::Wayland, "the override can force wayland");
        env.override_name = "bogus".into();
        env.i3_socket.clear();
        env.session_type.clear();
        env.wayland_display.clear();
        env.display = ":1".into();
        assert_eq!(detect(&env), SessionKind::X11, "an unknown override is ignored");
    }
}
