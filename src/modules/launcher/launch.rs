//! Running what the launcher picked: apps, commands, web searches, URLs and file actions.

use super::desktop_entry::{DesktopEntry, strip_field_codes};
use super::search::LauncherMode;
use super::submenu::{SubmenuAction, SubmenuEntry};
use super::visits::{VisitStore, visit_store_app_key, visit_store_file_key};
use crate::base::process::spawn_detached;
use crate::config::launcher_config as cfg;

/// Single-quoted for `sh`.
pub fn shell_quote(s: &str) -> String {
    format!("'{}'", s.replace('\'', "'\\''"))
}

fn url_encode(text: &str) -> String {
    let mut out = String::new();
    for b in text.bytes() {
        if b.is_ascii_alphanumeric() || b"-_.~".contains(&b) {
            out.push(b as char);
        } else {
            out.push_str(&format!("%{b:02X}"));
        }
    }
    out
}

pub fn make_search_url(text: &str, base: &str) -> String {
    let t = text.trim();
    if t.is_empty() { String::new() } else { format!("{base}{}", url_encode(t)) }
}

/// `^[a-zA-Z][a-zA-Z0-9+.-]*://`
fn has_scheme(t: &str) -> bool {
    let Some((scheme, _)) = t.split_once("://") else { return false };
    let mut chars = scheme.chars();
    chars.next().is_some_and(|c| c.is_ascii_alphabetic()) && chars.all(|c| c.is_ascii_alphanumeric() || "+.-".contains(c))
}

/// An optional `[:/]...` tail.
fn port_or_path_tail(rest: &str) -> bool {
    rest.is_empty() || rest.starts_with(':') || rest.starts_with('/')
}

/// `^\d{1,3}(?:\.\d{1,3}){3}([:/].*)?$`
fn is_ipv4(t: &str) -> bool {
    let end = t.find([':', '/']).unwrap_or(t.len());
    let parts: Vec<&str> = t[..end].split('.').collect();
    parts.len() == 4 && parts.iter().all(|p| (1..=3).contains(&p.len()) && p.bytes().all(|b| b.is_ascii_digit()))
}

/// `^[^\s@]+\.[^\s@]+$`
fn is_host_tld(t: &str) -> bool {
    !t.contains('@') && !t.contains(char::is_whitespace) && t.char_indices().any(|(i, c)| c == '.' && i > 0 && i + 1 < t.len())
}

/// `^[^\s/]+:\d+(?:/.*)?$`
fn is_host_port(t: &str) -> bool {
    let (host_port, _) = t.split_once('/').unwrap_or((t, ""));
    let Some((host, port)) = host_port.rsplit_once(':') else { return false };
    !host.is_empty() && !host.contains(char::is_whitespace) && !port.is_empty() && port.bytes().all(|b| b.is_ascii_digit())
}

/// A URL from what the user typed, or empty when it does not look like one.
pub fn normalize_url(text: &str) -> String {
    let t = text.trim();
    if t.is_empty() {
        return String::new();
    }
    if has_scheme(t) {
        return t.to_string();
    }
    if t.starts_with("//") {
        return format!("https:{t}");
    }
    if t.contains([' ', '\t']) {
        return String::new();
    }
    let localhost = t.strip_prefix("localhost").is_some_and(port_or_path_tail);
    if localhost || is_ipv4(t) || is_host_tld(t) || is_host_port(t) { format!("http://{t}") } else { String::new() }
}

pub fn app_command(entry: &DesktopEntry) -> String {
    let command = strip_field_codes(&entry.exec);
    if entry.terminal { format!("{} {command}", cfg::TERMINAL) } else { command }
}

fn spawn_with(app: &str, arg: &str) {
    spawn_detached(&format!("{app} {}", shell_quote(arg)));
}

fn open_url(url: &str) -> bool {
    if url.is_empty() {
        return false;
    }
    spawn_with(cfg::BROWSER, url);
    true
}

/// Runs a command, a web search or a URL; false when there was nothing to run.
pub fn launch_non_drun(mode: LauncherMode, query: &str) -> bool {
    match mode {
        LauncherMode::Run => {
            let command = query.trim();
            if command.is_empty() {
                return false;
            }
            let shell = std::env::var("SHELL").unwrap_or_else(|_| "/bin/sh".into());
            spawn_detached(&format!("{shell} -lic {}", shell_quote(command)));
            true
        }
        LauncherMode::Google => open_url(&make_search_url(query, cfg::GOOGLE_URL)),
        LauncherMode::Duckduckgo => open_url(&make_search_url(query, cfg::DUCKDUCKGO_URL)),
        LauncherMode::Youtube => open_url(&make_search_url(query, cfg::YOUTUBE_URL)),
        LauncherMode::Url => open_url(&normalize_url(query)),
        LauncherMode::Drun => false,
    }
}

pub fn launch_submenu_action(entry: &SubmenuEntry, visits: &mut VisitStore) {
    match entry.action {
        SubmenuAction::FileOpen => spawn_with(cfg::OPEN, &entry.path),
        SubmenuAction::DirOpenFileManager => spawn_with(cfg::FILE_MANAGER, &entry.path),
        SubmenuAction::DirOpenEditor => spawn_with(cfg::EDITOR, &entry.path),
        SubmenuAction::DirOpenTerminal => spawn_detached(&format!("cd {} && exec {}", shell_quote(&entry.path), cfg::TERMINAL)),
        _ => return,
    }
    visits.record(&visit_store_file_key(&entry.path));
}

pub fn launch_app(entry: &DesktopEntry, visits: &mut VisitStore) {
    spawn_detached(&app_command(entry));
    visits.record(&visit_store_app_key(&entry.id));
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::launcher_config as cfg;
    use crate::modules::launcher::desktop_entry::DesktopEntry;
    use crate::modules::launcher::search::LauncherMode;

    #[test]
    fn launcher_launch_urls() {
        assert_eq!(normalize_url("example.com"), "http://example.com", "bare host");
        assert_eq!(normalize_url("https://a.b/c"), "https://a.b/c", "scheme kept");
        assert_eq!(normalize_url("//example.com"), "https://example.com", "scheme-relative");
        assert_eq!(normalize_url("localhost:8080"), "http://localhost:8080", "localhost");
        assert_eq!(normalize_url("192.168.1.1:9000/path"), "http://192.168.1.1:9000/path", "ipv4 with port");
        assert_eq!(normalize_url("host:1234"), "http://host:1234", "host with port");
        assert!(normalize_url("two words").is_empty() && normalize_url("").is_empty(), "spaces and empty are not URLs");
        assert!(normalize_url("plainword").is_empty() && normalize_url("a@b.c").is_empty(), "no host shape, no URL");
        assert_eq!(make_search_url(" a b ", "q="), "q=a%20b", "search URL encodes");
        assert!(make_search_url("", "q=").is_empty(), "empty search URL");
        assert!(shell_quote("it's") == "'it'\\''s'" && shell_quote("").len() == 2, "shell quoting");
        assert_eq!(shell_quote("Bob's Files"), "'Bob'\\''s Files'", "shell quoting with spaces");
        let htop = DesktopEntry { id: "htop.desktop".into(), name: "htop".into(), exec: "htop %f".into(), terminal: true, ..DesktopEntry::default() };
        assert_eq!(app_command(&htop), format!("{} htop", cfg::TERMINAL), "terminal apps use the configured terminal wrapper");
        let plain = DesktopEntry { id: "x.desktop".into(), name: "x".into(), exec: "x --flag".into(), ..DesktopEntry::default() };
        assert_eq!(app_command(&plain), "x --flag", "plain apps run as they are");
        assert!(
            !launch_non_drun(LauncherMode::Run, "") && !launch_non_drun(LauncherMode::Google, "") && !launch_non_drun(LauncherMode::Drun, "x"),
            "empty queries and drun launch nothing"
        );
    }
}
