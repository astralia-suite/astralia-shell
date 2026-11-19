//! `.desktop` files: parsing, `Exec` field codes and the XDG directories they live in.

use std::collections::HashSet;
use std::io::BufRead;
use std::os::unix::fs::MetadataExt;

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct DesktopEntry {
    pub id: String,
    pub name: String,
    pub exec: String,
    pub icon: String,
    pub terminal: bool,
    pub no_display: bool,
    pub hidden: bool,
}

/// The `[Desktop Entry]` section of an application entry; `None` for other types or one without a
/// name or command.
pub fn parse_desktop_entry(input: impl BufRead, id: &str) -> Option<DesktopEntry> {
    let mut entry = DesktopEntry { id: id.to_string(), ..DesktopEntry::default() };
    let mut in_section = false;
    let mut application = true;
    for line in input.lines() {
        let Ok(line) = line else { break };
        let line = line.strip_suffix('\r').unwrap_or(&line);
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        if line.starts_with('[') {
            in_section = line == "[Desktop Entry]";
            continue;
        }
        if !in_section {
            continue;
        }
        let Some((key, value)) = line.split_once('=') else { continue };
        match key {
            "Name" if entry.name.is_empty() => entry.name = value.to_string(),
            "Exec" => entry.exec = value.to_string(),
            "Icon" => entry.icon = value.to_string(),
            "Terminal" => entry.terminal = value == "true",
            "NoDisplay" => entry.no_display = value == "true",
            "Hidden" => entry.hidden = value == "true",
            "Type" => application = value == "Application",
            _ => {}
        }
    }
    (application && !entry.name.is_empty() && !entry.exec.is_empty()).then_some(entry)
}

/// `Exec` without its `%` field codes; `%%` is a literal `%`, and a code's following space goes too
/// unless another code follows it.
pub fn strip_field_codes(exec: &str) -> String {
    let b = exec.as_bytes();
    let mut out = Vec::with_capacity(b.len());
    let mut i = 0;
    while i < b.len() {
        if b[i] != b'%' {
            out.push(b[i]);
            i += 1;
            continue;
        }
        if i + 1 >= b.len() {
            break;
        }
        i += 1;
        if b[i] == b'%' {
            out.push(b'%');
        } else if i + 1 < b.len() && b[i + 1] == b' ' && (i + 2 >= b.len() || b[i + 2] != b'%') {
            i += 1;
        }
        i += 1;
    }
    while out.last() == Some(&b' ') {
        out.pop();
    }
    String::from_utf8_lossy(&out).into_owned()
}

/// `$XDG_DATA_HOME/applications` (or `~/.local/share/applications`), then each `$XDG_DATA_DIRS`.
pub fn desktop_entry_dirs(data_home: Option<&str>, data_dirs: Option<&str>, home: Option<&str>) -> Vec<String> {
    let mut dirs = Vec::new();
    match (data_home.filter(|s| !s.is_empty()), home.filter(|s| !s.is_empty())) {
        (Some(data_home), _) => dirs.push(format!("{data_home}/applications")),
        (None, Some(home)) => dirs.push(format!("{home}/.local/share/applications")),
        _ => {}
    }
    let joined = data_dirs.filter(|s| !s.is_empty()).unwrap_or("/usr/local/share:/usr/share");
    dirs.extend(joined.split(':').filter(|p| !p.is_empty()).map(|p| format!("{p}/applications")));
    dirs
}

fn env_dirs() -> Vec<String> {
    let var = |name| std::env::var(name).ok();
    desktop_entry_dirs(var("XDG_DATA_HOME").as_deref(), var("XDG_DATA_DIRS").as_deref(), var("HOME").as_deref())
}

/// Every visible application; an id found in an earlier directory hides later ones.
pub fn scan_desktop_entries() -> Vec<DesktopEntry> {
    let mut result = Vec::new();
    let mut seen = HashSet::new();
    for dir in env_dirs() {
        let Ok(read) = std::fs::read_dir(&dir) else { continue };
        for ent in read.flatten() {
            let name = ent.file_name().to_string_lossy().into_owned();
            if !name.ends_with(".desktop") || !seen.insert(name.clone()) {
                continue;
            }
            let Ok(file) = std::fs::File::open(ent.path()) else { continue };
            if let Some(entry) = parse_desktop_entry(std::io::BufReader::new(file), &name).filter(|e| !e.no_display && !e.hidden) {
                result.push(entry);
            }
        }
    }
    result
}

/// Modification times of the directories and their entries; a change means a rescan.
pub fn desktop_entry_stamp() -> Vec<i64> {
    let mtime = |path: &std::path::Path| std::fs::metadata(path).map_or(-1, |m| m.mtime() * 1_000_000_000 + m.mtime_nsec());
    let mut stamp = Vec::new();
    for dir in env_dirs() {
        stamp.push(mtime(std::path::Path::new(&dir)));
        let Ok(read) = std::fs::read_dir(&dir) else { continue };
        for ent in read.flatten() {
            if ent.file_name().to_string_lossy().ends_with(".desktop") {
                stamp.push(mtime(&ent.path()));
            }
        }
    }
    stamp
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn launcher_desktop_entry() {
        let main = "[Desktop Entry]\nType=Application\nName=Foo\nName=Ignored\nExec=foo %U\nIcon=foo\nTerminal=true\n[Desktop Action x]\nName=X\n";
        let entry = parse_desktop_entry(main.as_bytes(), "foo.desktop").unwrap();
        assert!(entry.name == "Foo" && entry.terminal && entry.icon == "foo" && entry.exec == "foo %U", "desktop entry parses the main section");
        let hidden = "[Desktop Entry]\nType=Application\nName=H\nExec=h\nNoDisplay=true\n";
        assert!(parse_desktop_entry(hidden.as_bytes(), "h.desktop").unwrap().no_display, "NoDisplay is read");
        assert!(parse_desktop_entry("[Desktop Entry]\nType=Link\nName=L\nExec=l\n".as_bytes(), "l.desktop").is_none(), "non-applications are skipped");
        assert_eq!(strip_field_codes("foo %U --x %% %f"), "foo --x %", "field codes are stripped");
        assert_eq!(strip_field_codes("cmd %i %c %k end"), "cmd   end", "unused codes leave their spaces");
        assert_eq!(
            desktop_entry_dirs(None, Some("/x:/y"), Some("/home/u")),
            ["/home/u/.local/share/applications", "/x/applications", "/y/applications"],
            "desktop dirs follow XDG"
        );
    }
}
