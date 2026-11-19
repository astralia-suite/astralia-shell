//! A compact in-memory tree of every file and folder under a root, searched by basename.

use std::collections::HashSet;
use std::sync::atomic::{AtomicBool, Ordering};

use super::files::{FileEntry, basename_of, score_parts, split_query_parts};
use super::visits::{VisitStore, visit_store_file_key};

pub const NO_PARENT: u32 = u32::MAX;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Node {
    pub parent: u32,
    pub name_offset: u32,
    pub name_length: u16,
    pub is_dir: bool,
}

/// Every name, each followed by `/`, in one string, and a node per name pointing at its parent.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct FileIndex {
    pub root: String,
    pub nodes: Vec<Node>,
    pub names: String,
    /// `names` in ASCII lowercase, at the same offsets.
    pub lower: String,
}

impl FileIndex {
    fn name(&self, node: &Node) -> &str {
        &self.names[node.name_offset as usize..node.name_offset as usize + usize::from(node.name_length)]
    }
}

/// Walks `root` (symlinks and anything but files and folders skipped) until done or `cancel` is set.
/// Runs off the loop thread.
pub fn file_index_build(root: &str, cancel: &AtomicBool) -> FileIndex {
    let mut index = FileIndex { root: root.to_string(), ..FileIndex::default() };
    while index.root.len() > 1 && index.root.ends_with('/') {
        index.root.pop();
    }
    let mut pending = vec![(index.root.clone(), NO_PARENT)];
    while let Some((dir, parent)) = pending.pop() {
        if cancel.load(Ordering::Relaxed) {
            break;
        }
        let Ok(read) = std::fs::read_dir(&dir) else { continue };
        for ent in read.flatten() {
            let Ok(kind) = ent.file_type() else { continue };
            let name = ent.file_name();
            let Some(name) = name.to_str() else { continue };
            if !(kind.is_dir() || kind.is_file()) || index.nodes.len() >= NO_PARENT as usize || name.len() > usize::from(u16::MAX) {
                continue;
            }
            let id = index.nodes.len() as u32;
            index.nodes.push(Node { parent, name_offset: index.names.len() as u32, name_length: name.len() as u16, is_dir: kind.is_dir() });
            index.names.push_str(name);
            index.names.push('/');
            if kind.is_dir() {
                let base = if dir == "/" { "" } else { dir.as_str() };
                pending.push((format!("{base}/{name}"), id));
            }
        }
    }
    index.lower = index.names.to_ascii_lowercase();
    index.nodes.shrink_to_fit();
    index.names.shrink_to_fit();
    index
}

/// The full path of `node`; a folder's ends with `/`, as `fd` prints it.
pub fn file_index_path(index: &FileIndex, node: u32) -> String {
    let mut chain = Vec::new();
    let mut at = node;
    while at != NO_PARENT {
        let n = &index.nodes[at as usize];
        chain.push(index.name(n));
        at = n.parent;
    }
    let mut path = if index.root == "/" { String::new() } else { index.root.clone() };
    for name in chain.iter().rev() {
        path.push('/');
        path.push_str(name);
    }
    if index.nodes[node as usize].is_dir {
        path.push('/');
    }
    path
}

struct Candidate {
    node: u32,
    visits: i32,
    score: f32,
}

/// The best folders, then the best files, whose basename holds the query's parts in order; at
/// most `max_results` of each, visited ones first.
pub fn file_index_search(index: &FileIndex, query: &str, visits: &VisitStore, max_results: usize) -> Vec<FileEntry> {
    let parts = split_query_parts(query);
    if parts.is_empty() || max_results == 0 {
        return Vec::new();
    }
    let prefix = visit_store_file_key("");
    let visited: HashSet<String> = visits.counts.keys().filter_map(|k| k.strip_prefix(&prefix)).map(basename_of).collect();
    let (mut dirs, mut files) = (Vec::new(), Vec::new());
    let lower = index.lower.as_str();
    let mut from = 0;
    while let Some(found) = lower[from..].find(parts[0].as_str()) {
        let at = from + found;
        let i = index.nodes.partition_point(|n| n.name_offset as usize <= at) - 1;
        let node = &index.nodes[i];
        let start = node.name_offset as usize;
        let end = start + usize::from(node.name_length);
        let score = score_parts(&lower[start..end], &parts);
        if score >= 0.0 {
            let count = if visited.contains(index.name(node)) { visits.get(&visit_store_file_key(&file_index_path(index, i as u32))) } else { 0 };
            let list = if node.is_dir { &mut dirs } else { &mut files };
            list.push(Candidate { node: i as u32, visits: count, score });
        }
        from = end;
    }
    let mut out = Vec::new();
    for list in [&mut dirs, &mut files] {
        list.sort_by(|a, b| {
            let name = |c: &Candidate| index.name(&index.nodes[c.node as usize]).to_ascii_lowercase();
            b.visits.cmp(&a.visits).then(b.score.total_cmp(&a.score)).then_with(|| name(a).cmp(&name(b)))
        });
        for c in list.iter().take(max_results) {
            let node = &index.nodes[c.node as usize];
            out.push(FileEntry { name: index.name(node).to_string(), path: file_index_path(index, c.node), is_dir: node.is_dir, score: c.score });
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::modules::launcher::files::score_path;
    use crate::modules::launcher::visits::{VisitStore, visit_store_file_key};
    use std::path::Path;
    use std::sync::atomic::AtomicBool;

    fn touch(path: &Path) {
        std::fs::write(path, "x").unwrap();
    }

    fn has(found: &[FileEntry], name: &str, is_dir: bool) -> bool {
        found.iter().any(|e| e.name == name && e.is_dir == is_dir)
    }

    #[test]
    fn launcher_index() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        std::fs::create_dir_all(root.join("Projects/.hidden_dir")).unwrap();
        touch(&root.join("Projects/report.pdf"));
        touch(&root.join("Projects/.hidden_dir/secret_report.txt"));
        touch(&root.join("notes.txt"));
        touch(&root.join("my-notes.txt"));
        std::os::unix::fs::symlink(root.join("notes.txt"), root.join("link_notes.txt")).unwrap();
        std::os::unix::fs::symlink(root.join("Projects"), root.join("link_projects")).unwrap();
        let root_s = root.to_str().unwrap().to_string();

        let cancel = AtomicBool::new(false);
        let index = file_index_build(&format!("{root_s}/"), &cancel);
        assert_eq!(index.root, root_s, "the index root drops a trailing slash");
        assert_eq!(index.nodes.len(), 6, "files and folders are indexed, symlinks and the root are not");

        let mut visits = VisitStore::default();
        let found = file_index_search(&index, "REPORT", &visits, 20);
        assert!(found.len() == 2 && has(&found, "report.pdf", false) && has(&found, "secret_report.txt", false), "matching ignores case and keeps hidden entries");
        for entry in &found {
            assert_eq!(entry.score, score_path(&entry.name, "REPORT"), "the index scores like score_path");
            let sub = if entry.name == "report.pdf" { "Projects" } else { "Projects/.hidden_dir" };
            assert_eq!(entry.path, format!("{root_s}/{sub}/{}", entry.name), "a file path is rebuilt from the tree");
        }

        let dirs = file_index_search(&index, "proj", &visits, 20);
        assert!(dirs.len() == 1 && dirs[0].is_dir && dirs[0].path == format!("{root_s}/Projects/"), "a folder path ends with a slash, like fd");
        assert_eq!(file_index_search(&index, "rep*pdf", &visits, 20).len(), 1, "ordered parts match");
        assert!(file_index_search(&index, "pdf*rep", &visits, 20).is_empty(), "parts out of order do not match");
        assert!(file_index_search(&index, "Projects/report", &visits, 20).is_empty(), "a query with a slash matches no basename");
        assert!(file_index_search(&index, "", &visits, 20).is_empty(), "an empty query finds nothing");

        let notes = file_index_search(&index, "notes", &visits, 1);
        assert!(notes.len() == 1 && notes[0].name == "notes.txt", "the best score survives the cut");
        visits.counts.insert(visit_store_file_key(&format!("{root_s}/my-notes.txt")), 3);
        let notes = file_index_search(&index, "notes", &visits, 1);
        assert!(notes.len() == 1 && notes[0].name == "my-notes.txt", "a visited file outranks a better score");

        let stopped = file_index_build(&root_s, &AtomicBool::new(true));
        assert!(stopped.nodes.is_empty(), "a cancelled build stops");
    }
}
