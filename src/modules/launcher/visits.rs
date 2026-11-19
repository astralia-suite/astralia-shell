//! How often each app and file was opened from the launcher, kept in a state file.

use std::collections::BTreeMap;

use crate::config::launcher_config as cfg;

pub fn visit_store_app_key(desktop_id: &str) -> String {
    format!("app:{desktop_id}")
}

pub fn visit_store_file_key(file_path: &str) -> String {
    format!("file:{file_path}")
}

/// `$XDG_STATE_HOME/astralia-shell/launcher_visits`.
pub fn visit_store_path(state_home: Option<&str>, home: Option<&str>) -> String {
    let dir = match state_home.filter(|s| !s.is_empty()) {
        Some(state) => state.to_string(),
        None => format!("{}/.local/state", home.unwrap_or("")),
    };
    format!("{dir}/{}", cfg::VISITS_FILE)
}

/// Counts by key (`app:<id>` or `file:<path>`), saved after every visit.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct VisitStore {
    pub counts: BTreeMap<String, i32>,
    pub path: String,
}

impl VisitStore {
    /// Whitespace-separated key and count pairs; a missing or broken file gives what parsed.
    pub fn load(path: &str) -> VisitStore {
        let mut store = VisitStore { path: path.to_string(), ..VisitStore::default() };
        let text = std::fs::read_to_string(path).unwrap_or_default();
        let mut words = text.split_whitespace();
        while let (Some(key), Some(count)) = (words.next(), words.next()) {
            let Ok(count) = count.parse() else { break };
            store.counts.insert(key.to_string(), count);
        }
        store
    }

    /// The store at the default path.
    pub fn load_default() -> VisitStore {
        let var = |name| std::env::var(name).ok();
        VisitStore::load(&visit_store_path(var("XDG_STATE_HOME").as_deref(), var("HOME").as_deref()))
    }

    pub fn get(&self, key: &str) -> i32 {
        self.counts.get(key).copied().unwrap_or(0)
    }

    pub fn record(&mut self, key: &str) {
        if key.is_empty() {
            return;
        }
        *self.counts.entry(key.to_string()).or_default() += 1;
        if let Some(dir) = std::path::Path::new(&self.path).parent() {
            let _ = std::fs::create_dir_all(dir);
        }
        let text: String = self.counts.iter().map(|(k, n)| format!("{k}\t{n}\n")).collect();
        if let Err(e) = std::fs::write(&self.path, text) {
            ::log::error!("launcher: cannot write {}: {e}", self.path);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn launcher_visit_store() {
        assert_eq!(visit_store_path(Some(""), Some("/home/u")), "/home/u/.local/state/astralia-shell/launcher_visits", "visit store path from home");
        assert_eq!(visit_store_path(Some("/s"), Some("/home/u")), "/s/astralia-shell/launcher_visits", "visit store path from state home");
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("nested/visits").to_str().unwrap().to_string();
        let mut store = VisitStore::load(&path);
        assert_eq!(store.get("app:x"), 0, "unknown key has no visits");
        store.record(&visit_store_app_key("firefox.desktop"));
        store.record(&visit_store_app_key("firefox.desktop"));
        store.record(&visit_store_file_key("/home/u/notes.txt"));
        store.record("");
        let reloaded = VisitStore::load(&path);
        assert_eq!(reloaded.get("app:firefox.desktop"), 2, "app visits persist");
        assert_eq!(reloaded.get("file:/home/u/notes.txt"), 1, "file visits persist");
        assert_eq!(reloaded.counts.len(), 2, "empty keys are ignored");
    }
}
