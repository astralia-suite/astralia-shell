//! What the field asks for: the mode from its prefix, and the ranked app and file results.

use super::apps::{ScoredApp, to_lower};
use super::desktop_entry::DesktopEntry;
use super::files::FileEntry;
use super::visits::{VisitStore, visit_store_app_key, visit_store_file_key};

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum LauncherMode {
    #[default]
    Drun,
    Run,
    Google,
    Duckduckgo,
    Youtube,
    Url,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ModeQuery {
    pub mode: LauncherMode,
    pub query: String,
}

const PREFIXES: [(&str, LauncherMode); 5] =
    [(">", LauncherMode::Run), ("gg", LauncherMode::Google), ("ddg", LauncherMode::Duckduckgo), ("yt", LauncherMode::Youtube), ("url", LauncherMode::Url)];

/// The mode a prefix picks and the query after it; a word prefix needs a space or the end after it.
pub fn detect_mode_and_query(raw: &str) -> ModeQuery {
    let trimmed = raw.trim_start();
    for (prefix, mode) in PREFIXES {
        let Some(rest) = trimmed.strip_prefix(prefix) else { continue };
        let word = prefix.bytes().all(|b| b.is_ascii_alphanumeric());
        if word && rest.chars().next().is_some_and(|c| !c.is_whitespace()) {
            continue;
        }
        return ModeQuery { mode, query: rest.trim_start().to_string() };
    }
    ModeQuery { mode: LauncherMode::Drun, query: trimmed.to_string() }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DrunKind {
    App,
    Dir,
    File,
}

/// A row of the default mode: an app by its index in the entry list, or a file or directory.
#[derive(Clone, Debug, PartialEq)]
pub struct DrunResult {
    pub kind: DrunKind,
    pub app: Option<usize>,
    pub file: FileEntry,
}

/// Apps, then directories, then files; within each, more visits, then a higher score, then the name.
pub fn combined_drun_results(entries: &[DesktopEntry], apps: &[ScoredApp], files: &[FileEntry], visits: &VisitStore, max_results: usize) -> Vec<DrunResult> {
    struct Ranked {
        result: DrunResult,
        tier: u8,
        visits: i32,
        score: f32,
        name: String,
    }
    let mut ranked: Vec<Ranked> = apps
        .iter()
        .map(|a| {
            let e = &entries[a.entry];
            Ranked {
                result: DrunResult { kind: DrunKind::App, app: Some(a.entry), file: FileEntry::default() },
                tier: 0,
                visits: visits.get(&visit_store_app_key(&e.id)),
                score: a.score,
                name: to_lower(&e.name),
            }
        })
        .collect();
    ranked.extend(files.iter().map(|f| Ranked {
        result: DrunResult { kind: if f.is_dir { DrunKind::Dir } else { DrunKind::File }, app: None, file: f.clone() },
        tier: if f.is_dir { 1 } else { 2 },
        visits: visits.get(&visit_store_file_key(&f.path)),
        score: f.score,
        name: to_lower(&f.name),
    }));
    ranked.sort_by(|a, b| {
        a.tier.cmp(&b.tier).then(b.visits.cmp(&a.visits)).then(b.score.total_cmp(&a.score)).then_with(|| a.name.cmp(&b.name))
    });
    ranked.into_iter().take(max_results).map(|r| r.result).collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::modules::launcher::apps::ScoredApp;
    use crate::modules::launcher::desktop_entry::DesktopEntry;
    use crate::modules::launcher::files::FileEntry;
    use crate::modules::launcher::visits::{VisitStore, visit_store_app_key};

    fn mode(raw: &str) -> ModeQuery {
        detect_mode_and_query(raw)
    }

    #[test]
    fn launcher_modes() {
        assert_eq!(mode("  fire").mode, LauncherMode::Drun, "plain text is drun");
        assert_eq!(mode("  fire").query, "fire", "leading space is trimmed");
        assert!(mode("").mode == LauncherMode::Drun && mode("").query.is_empty(), "empty is drun");
        assert!(mode("> ls -l").mode == LauncherMode::Run && mode("> ls -l").query == "ls -l", "> is run mode");
        assert_eq!(mode("gg cats").mode, LauncherMode::Google, "gg is google");
        assert_eq!(mode("gg cat pictures").query, "cat pictures", "gg query");
        assert_eq!(mode("ggx").mode, LauncherMode::Drun, "word prefix needs a space");
        assert_eq!(mode("ggwp").mode, LauncherMode::Drun, "gg inside a word is not a prefix");
        assert_eq!(mode("yt").mode, LauncherMode::Youtube, "bare prefix switches mode");
        assert_eq!(mode("  yt lofi beats").query, "lofi beats", "yt query after leading space");
        assert_eq!(mode("ddg x").mode, LauncherMode::Duckduckgo, "ddg is duckduckgo");
        assert_eq!(mode("url example.com").query, "example.com", "url query");
    }

    #[test]
    fn launcher_drun_results() {
        let entries = [
            DesktopEntry { id: "a.desktop".into(), name: "Alpha".into(), exec: "a".into(), ..DesktopEntry::default() },
            DesktopEntry { id: "b.desktop".into(), name: "Beta".into(), exec: "b".into(), ..DesktopEntry::default() },
        ];
        let apps = [ScoredApp { entry: 0, score: 900.0 }, ScoredApp { entry: 1, score: 100.0 }];
        let file = |name: &str, path: &str, is_dir, score| FileEntry { name: name.into(), path: path.into(), is_dir, score };
        let files = [file("f", "/f", false, 999.0), file("d", "/d", true, 1.0)];
        let mut visits = VisitStore::default();
        visits.counts.insert(visit_store_app_key("b.desktop"), 3);
        let results = combined_drun_results(&entries, &apps, &files, &visits, 3);
        assert_eq!(results.len(), 3, "results are capped");
        assert!(results[0].app == Some(1) && results[1].app == Some(0), "visits outrank score among apps");
        assert_eq!(results[2].kind, DrunKind::Dir, "dirs come before files");
        assert_eq!(combined_drun_results(&entries, &apps, &files, &visits, 10).len(), 4, "all results fit under the cap");
    }
}
