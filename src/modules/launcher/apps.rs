//! Matching applications by name.

use super::desktop_entry::DesktopEntry;

/// An application that matched, by its index in the entry list.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ScoredApp {
    pub entry: usize,
    pub score: f32,
}

/// ASCII lowercase, byte by byte.
pub fn to_lower(s: &str) -> String {
    s.to_ascii_lowercase()
}

/// Higher is better, negative is no match: a prefix beats an inner match, earlier and shorter win.
pub fn score_app(name: &str, query: &str) -> f32 {
    let (n, q) = (to_lower(name), to_lower(query));
    if n.is_empty() || q.is_empty() {
        return -1.0;
    }
    let Some(idx) = n.find(&q) else { return -1.0 };
    let mut score = if idx == 0 { 1000.0 } else { 500.0 };
    score -= idx.min(200) as f32;
    score -= n.len().min(200) as f32 / 10.0;
    score
}

pub fn search_apps(entries: &[DesktopEntry], query: &str) -> Vec<ScoredApp> {
    if query.is_empty() {
        return Vec::new();
    }
    entries.iter().enumerate().filter_map(|(i, e)| Some(score_app(&e.name, query)).filter(|s| *s >= 0.0).map(|score| ScoredApp { entry: i, score })).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn app(id: &str, name: &str) -> DesktopEntry {
        DesktopEntry { id: id.into(), name: name.into(), exec: id.into(), ..DesktopEntry::default() }
    }

    #[test]
    fn launcher_app_scoring() {
        assert!(score_app("Firefox", "fire") > score_app("Wildfire", "fire"), "prefix match scores higher");
        assert!(score_app("Firefox", "fire") > score_app("Bonfire", "fire"), "earlier match scores higher");
        assert!(score_app("Firefox", "zz") < 0.0, "no match is negative");
        assert!(score_app("Firefox", "") < 0.0 && score_app("", "fire") < 0.0, "empty sides never match");
        let entries = [app("firefox.desktop", "Firefox"), app("bonfire.desktop", "Bonfire"), app("calc.desktop", "Calculator")];
        let found = search_apps(&entries, "fire");
        assert!(found.len() == 2 && entries[found[0].entry].name == "Firefox", "search_apps drops non-matches and ranks");
        assert!(search_apps(&entries, "").is_empty(), "empty query finds nothing");
    }
}
