//! Who is logged in: the display name, the avatar image and the uptime, for the settings menu.

use std::ffi::CStr;

/// The first GECOS field when set, else the login name, else `unknown`.
pub fn display_name(gecos: &str, login: &str) -> String {
    let first = gecos.split(',').next().unwrap_or_default();
    if !first.is_empty() {
        return first.to_string();
    }
    if login.is_empty() { "unknown".into() } else { login.to_string() }
}

/// `(gecos, login)` of the current user.
fn passwd() -> Option<(String, String)> {
    // SAFETY: `getpwuid` returns a pointer into static storage or null; it is read at once, on the
    // loop thread, which is the only caller.
    unsafe {
        let entry = libc::getpwuid(libc::getuid());
        if entry.is_null() {
            return None;
        }
        let text = |p: *const libc::c_char| if p.is_null() { String::new() } else { CStr::from_ptr(p).to_string_lossy().into_owned() };
        Some((text((*entry).pw_gecos), text((*entry).pw_name)))
    }
}

pub fn user_name() -> String {
    passwd().map_or_else(|| "unknown".into(), |(gecos, login)| display_name(&gecos, &login))
}

/// The login name PAM authenticates; `unknown` when the passwd entry is missing.
pub fn login_name() -> String {
    passwd().map_or_else(|| "unknown".into(), |(_, login)| login)
}

/// The login name of `uid`; `None` when it has no passwd entry. Safe from any thread.
pub fn login_name_of(uid: u32) -> Option<String> {
    let mut buffer = vec![0 as libc::c_char; 4096];
    // SAFETY: `getpwuid_r` writes only into `entry` and `buffer`, both live for the call; the
    // name is copied out before they drop.
    unsafe {
        let mut entry: libc::passwd = std::mem::zeroed();
        let mut result = std::ptr::null_mut();
        if libc::getpwuid_r(uid, &mut entry, buffer.as_mut_ptr(), buffer.len(), &mut result) != 0 || result.is_null() || entry.pw_name.is_null() {
            return None;
        }
        Some(CStr::from_ptr(entry.pw_name).to_string_lossy().into_owned()).filter(|n| !n.is_empty())
    }
}

/// The first readable of `~/.face`, `~/.face.icon` and the AccountsService icon; empty when none.
pub fn face_path() -> String {
    let mut candidates = Vec::new();
    if let Some(home) = std::env::var("HOME").ok().filter(|h| !h.is_empty()) {
        candidates.push(format!("{home}/.face"));
        candidates.push(format!("{home}/.face.icon"));
    }
    if let Some((_, login)) = passwd().filter(|(_, login)| !login.is_empty()) {
        candidates.push(format!("/var/lib/AccountsService/icons/{login}"));
    }
    candidates.into_iter().find(|p| std::fs::File::open(p).is_ok()).unwrap_or_default()
}

pub fn uptime_text(seconds: libc::c_long) -> String {
    format!("Up: {:02}:{:02}", seconds / 3600, (seconds % 3600) / 60)
}

/// `Up: HH:MM`; empty when the kernel does not say.
pub fn uptime() -> String {
    let mut info = std::mem::MaybeUninit::<libc::sysinfo>::zeroed();
    // SAFETY: `sysinfo` fills the struct it is given and touches nothing else.
    if unsafe { libc::sysinfo(info.as_mut_ptr()) } != 0 {
        return String::new();
    }
    // SAFETY: filled by the successful call above.
    uptime_text(unsafe { info.assume_init() }.uptime)
}

/// `PRETTY_NAME` from an os-release file, unquoted; `Linux` when it is missing.
pub fn parse_os_name(os_release: &str) -> String {
    for line in os_release.lines() {
        let Some(value) = line.strip_prefix("PRETTY_NAME=") else { continue };
        let value = if value.len() >= 2 && value.starts_with('"') && value.ends_with('"') { &value[1..value.len() - 1] } else { value };
        return value.to_string();
    }
    "Linux".into()
}

pub fn os_name() -> String {
    parse_os_name(&std::fs::read_to_string("/etc/os-release").unwrap_or_default())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn user_os_name() {
        assert_eq!(parse_os_name("NAME=\"Arch Linux\"\nPRETTY_NAME=\"Arch Linux\"\nID=arch\n"), "Arch Linux", "quoted pretty name");
        assert_eq!(parse_os_name("PRETTY_NAME=Plain\n"), "Plain", "unquoted pretty name");
        assert_eq!(parse_os_name("ID=arch\n"), "Linux", "missing pretty name falls back");
    }

    #[test]
    fn user_text() {
        assert_eq!(display_name("Ada Lovelace,,,", "ada"), "Ada Lovelace");
        assert_eq!(display_name("", "ada"), "ada");
        assert_eq!(display_name(",room", ""), "unknown");
        assert_eq!(uptime_text(0), "Up: 00:00");
        assert_eq!(uptime_text(100 * 3600 + 7 * 60 + 59), "Up: 100:07");
    }
}
