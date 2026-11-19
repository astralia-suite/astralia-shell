//! Paths for the launcher: naming, eliding, glob patterns, path scoring and `fd`.

use super::apps::to_lower;
use crate::config::launcher_config as cfg;

#[derive(Clone, Debug, Default, PartialEq)]
pub struct FileEntry {
    pub name: String,
    pub path: String,
    pub is_dir: bool,
    pub score: f32,
}

pub fn basename_of(path: &str) -> String {
    let p = path.trim_end_matches('/');
    if p.is_empty() {
        return if path.is_empty() { String::new() } else { "/".into() };
    }
    p.rsplit_once('/').map_or(p, |(_, name)| name).to_string()
}

pub fn parent_of(path: &str) -> String {
    let p = path.trim_end_matches('/');
    match p.rfind('/') {
        None | Some(0) => "/".into(),
        Some(slash) => p[..slash].to_string(),
    }
}

/// `path` with a leading `home` (not `/`) written as `~`.
pub fn collapse_home(path: &str, home: &str) -> String {
    if home.is_empty() || home == "/" {
        return path.to_string();
    }
    match path.strip_prefix(home) {
        Some("") => "~".into(),
        Some(rest) if rest.starts_with('/') => format!("~{rest}"),
        _ => path.to_string(),
    }
}

/// At most `max_chars` characters, the last one an ellipsis when cut.
pub fn elide(s: &str, max_chars: usize) -> String {
    if max_chars == 0 || s.chars().count() <= max_chars {
        return s.to_string();
    }
    let mut out: String = s.chars().take(max_chars - 1).collect();
    out.push('…');
    out
}

/// At most `max_chars` characters, cut in the middle.
pub fn elide_middle(s: &str, max_chars: usize) -> String {
    let count = s.chars().count();
    if count <= max_chars {
        return s.to_string();
    }
    if max_chars < 2 {
        return elide(s, max_chars);
    }
    let keep = max_chars - 1;
    let head = keep.div_ceil(2);
    let tail = keep - head;
    let start: String = s.chars().take(head).collect();
    let end: String = s.chars().skip(count - tail).collect();
    format!("{start}…{end}")
}

/// An `fd --glob` pattern: a bare word matches anywhere in a name, a path anywhere below the root.
pub fn to_glob_pattern(query: &str) -> String {
    let q = query.trim_matches(' ');
    if q.is_empty() {
        String::new()
    } else if q.starts_with("**/") || q.starts_with('/') {
        q.to_string()
    } else if q.contains('/') {
        format!("**/{q}")
    } else {
        format!("**/*{q}*")
    }
}

/// The lowercase query, split on `*` into the parts a name must contain in order.
pub fn split_query_parts(query: &str) -> Vec<String> {
    let lower = to_lower(query);
    let q = lower.trim_matches(' ');
    if q.is_empty() {
        return Vec::new();
    }
    if !q.contains('*') {
        return vec![q.to_string()];
    }
    q.split('*').map(|p| p.trim_matches(' ')).filter(|p| !p.is_empty()).map(String::from).collect()
}

pub fn score_path(name: &str, query: &str) -> f32 {
    score_parts(&to_lower(name), &split_query_parts(query))
}

/// The first part scores like an app name; each later part adds for ending near the name's end.
pub fn score_parts(name_lower: &str, parts: &[String]) -> f32 {
    if name_lower.is_empty() || parts.is_empty() {
        return -1.0;
    }
    let mut score = 0.0;
    let mut cursor = 0;
    for (i, part) in parts.iter().enumerate() {
        let Some(found) = name_lower[cursor..].find(part.as_str()) else { return -1.0 };
        let idx = cursor + found;
        if i == 0 {
            score += if idx == 0 { 1000.0 } else { 500.0 };
            score -= idx.min(200) as f32;
        } else {
            let end_distance = name_lower.len() - (idx + part.len());
            score += 200.0;
            score -= end_distance.min(200) as f32;
        }
        cursor = idx + part.len();
    }
    score - name_lower.len().min(200) as f32 / 10.0
}

/// `fd` arguments for files or directories matching `pattern` under `root`; `depth` 0 is unlimited.
pub fn fd_search_argv(pattern: &str, root: &str, is_dir: bool, max_results: i32, depth: i32, full_path: bool) -> Vec<String> {
    let mut argv: Vec<String> = ["fd", "--glob", "--ignore-case"].map(String::from).into();
    if full_path {
        argv.push("--full-path".into());
    }
    let max = max_results.to_string();
    argv.extend(["--type", if is_dir { "d" } else { "f" }, "--hidden", "--no-ignore", "--absolute-path", "--color", "never", "--max-results", &max].map(String::from));
    if depth > 0 {
        argv.extend(["--max-depth".to_string(), depth.to_string()]);
    }
    argv.extend(["--".to_string(), if pattern.is_empty() { "*".into() } else { pattern.to_string() }, root.to_string()]);
    argv
}

/// One entry per non-blank line of `fd`'s output.
pub fn fd_search_parse_output(raw: &str, is_dir: bool) -> Vec<FileEntry> {
    raw.lines()
        .map(|l| l.trim_matches([' ', '\t']))
        .filter(|l| !l.is_empty())
        .map(|path| FileEntry { name: basename_of(path), path: path.to_string(), is_dir, score: 0.0 })
        .collect()
}

/// The directories or files directly in `path`, hidden ones included, at most the listing limit.
pub fn list_directory(path: &str, want_dirs: bool) -> Vec<FileEntry> {
    let Ok(read) = std::fs::read_dir(path) else { return Vec::new() };
    read.flatten()
        .filter(|e| e.file_type().is_ok_and(|t| if want_dirs { t.is_dir() } else { t.is_file() }))
        .take(cfg::LISTING_MAX_RESULTS as usize)
        .map(|e| {
            let full = e.path().to_string_lossy().into_owned();
            FileEntry { name: basename_of(&full), path: full, is_dir: want_dirs, score: 0.0 }
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn launcher_text() {
        assert_eq!(elide("abcdef", 4), "abc…", "elide keeps max - 1 chars");
        assert_eq!(elide("héllo", 5), "héllo", "elide counts UTF-8 chars");
        assert_eq!(elide_middle("abcdefghij", 5), "ab…ij", "elide_middle keeps both ends");
        assert_eq!(collapse_home("/home/u/docs", "/home/u"), "~/docs", "home collapses");
        assert_eq!(collapse_home("/home/user2", "/home/u"), "/home/user2", "prefix without slash is kept");
        assert!(basename_of("/a/b/") == "b" && basename_of("/") == "/", "basename_of");
        assert_eq!(basename_of("/home/user/file.txt"), "file.txt", "basename_of a file");
        assert!(parent_of("/a/b") == "/a" && parent_of("/a") == "/", "parent_of");
    }

    #[test]
    fn launcher_path_scoring() {
        assert_eq!(to_glob_pattern(" notes "), "**/*notes*", "glob wraps a bare word");
        assert_eq!(to_glob_pattern("a/b"), "**/a/b", "glob anchors a path");
        assert_eq!(to_glob_pattern("/abs/path"), "/abs/path", "glob keeps an absolute path");
        assert_eq!(to_glob_pattern("**/already"), "**/already", "glob keeps an anchored pattern");
        assert!(to_glob_pattern("").is_empty(), "empty glob");
        assert_eq!(split_query_parts("Foo * bar"), ["foo", "bar"], "query splits on *");
        assert_eq!(split_query_parts("hello"), ["hello"], "single part");
        assert!(split_query_parts("").is_empty(), "no parts");
        assert!(score_path("report.pdf", "rep*pdf") > 0.0, "ordered parts match");
        assert!(score_path("report.pdf", "pdf*rep") < 0.0, "out of order parts fail");
        assert!(score_path("notes.txt", "notes") > score_path("my-notes.txt", "notes"), "earlier path match scores higher");
        assert!(score_path("foobarxxxxxxxxxxxxxxxxxxxx", "foo*bar") < score_path("fooxxxxxxxxxxxxxxxxxxxxbar", "foo*bar"), "tight parts score higher");
        let parsed = fd_search_parse_output("/a/b\n\n  /c/d  \n", true);
        assert!(parsed.len() == 2 && parsed[1].name == "d" && parsed[1].is_dir, "fd output parses");
    }

    #[test]
    fn launcher_listing() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path().to_str().unwrap();
        std::fs::create_dir(dir.path().join("subdir")).unwrap();
        std::fs::write(dir.path().join("hello.txt"), "x").unwrap();
        let argv = fd_search_argv("**/*hello*", root, false, 10, 0, true);
        assert_eq!(argv.first().map(String::as_str), Some("fd"), "fd search argv starts with fd");
        let files = list_directory(root, false);
        let dirs = list_directory(root, true);
        assert!(files.len() == 1 && files[0].name == "hello.txt" && !files[0].is_dir, "list_directory lists files");
        assert!(dirs.len() == 1 && dirs[0].name == "subdir" && dirs[0].is_dir, "list_directory lists directories");
    }
}
