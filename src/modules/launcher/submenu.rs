//! The browse submenu: a directory's actions, folders and files, and the actions of one file.

use super::files::{FileEntry, parent_of};

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum SubmenuScreen {
    #[default]
    Search,
    Browse,
    DirActions,
    FileActions,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum SubmenuIcon {
    #[default]
    None,
    ArrowRight,
    ArrowLeft,
    Code,
    Terminal,
    FolderOpen,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum SubmenuAction {
    /// A listed directory or file: open it in the submenu.
    #[default]
    None,
    OpenOptions,
    OpenContainingDir,
    PrevDir,
    DirOpenFileManager,
    DirOpenEditor,
    DirOpenTerminal,
    FileOpen,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct SubmenuEntry {
    pub name: String,
    pub path: String,
    pub is_dir: bool,
    pub icon: SubmenuIcon,
    pub action: SubmenuAction,
}

/// Lists the directories (`true`) or files of a path.
pub type DirLister<'a> = dyn Fn(&str, bool) -> Vec<FileEntry> + 'a;

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct SubmenuState {
    pub screen: SubmenuScreen,
    pub current_path: String,
    pub came_from_browse: bool,
    pub items: Vec<SubmenuEntry>,
}

fn entry(name: &str, path: &str, is_dir: bool, icon: SubmenuIcon, action: SubmenuAction) -> SubmenuEntry {
    SubmenuEntry { name: name.into(), path: path.into(), is_dir, icon, action }
}

impl SubmenuState {
    pub fn close(&mut self) {
        *self = SubmenuState::default();
    }

    pub fn open_directory(&mut self, path: &str, list_dir: &DirLister) {
        self.screen = SubmenuScreen::Browse;
        self.current_path = path.to_string();
        self.items = vec![
            entry("Open Directory", path, true, SubmenuIcon::ArrowRight, SubmenuAction::OpenOptions),
            entry("Previous Directory", path, true, SubmenuIcon::ArrowLeft, SubmenuAction::PrevDir),
        ];
        for dirs in [true, false] {
            for f in list_dir(path, dirs) {
                self.items.push(entry(&f.name, &f.path, dirs, SubmenuIcon::None, SubmenuAction::None));
            }
        }
    }

    pub fn open_directory_actions(&mut self, path: &str) {
        self.screen = SubmenuScreen::DirActions;
        self.current_path = path.to_string();
        self.items = vec![
            entry("Open Directory in File Manager", path, true, SubmenuIcon::ArrowRight, SubmenuAction::DirOpenFileManager),
            entry("Open Directory in Editor", path, true, SubmenuIcon::Code, SubmenuAction::DirOpenEditor),
            entry("Open Directory in Terminal", path, true, SubmenuIcon::Terminal, SubmenuAction::DirOpenTerminal),
        ];
    }

    pub fn open_file_actions(&mut self, path: &str) {
        self.came_from_browse = self.screen == SubmenuScreen::Browse;
        self.screen = SubmenuScreen::FileActions;
        self.current_path = path.to_string();
        self.items = vec![
            entry("Open File", path, false, SubmenuIcon::ArrowRight, SubmenuAction::FileOpen),
            entry("Open Containing Directory", &parent_of(path), true, SubmenuIcon::FolderOpen, SubmenuAction::OpenContainingDir),
        ];
    }

    /// Moves within the submenu; false for an action the caller runs.
    pub fn handle_entry(&mut self, e: &SubmenuEntry, list_dir: &DirLister) -> bool {
        match e.action {
            SubmenuAction::OpenOptions | SubmenuAction::OpenContainingDir => self.open_directory_actions(&e.path),
            SubmenuAction::None if e.is_dir => self.open_directory(&e.path, list_dir),
            SubmenuAction::None => self.open_file_actions(&e.path),
            SubmenuAction::PrevDir => {
                if self.current_path != "/" {
                    let parent = parent_of(&self.current_path);
                    self.open_directory(&parent, list_dir);
                }
            }
            _ => return false,
        }
        true
    }

    /// One screen back; false on the search screen.
    pub fn go_back(&mut self, list_dir: &DirLister) -> bool {
        match self.screen {
            SubmenuScreen::DirActions => {
                let path = self.current_path.clone();
                self.open_directory(&path, list_dir);
            }
            SubmenuScreen::FileActions if self.came_from_browse => {
                let parent = parent_of(&self.current_path);
                self.open_directory(&parent, list_dir);
            }
            SubmenuScreen::FileActions | SubmenuScreen::Browse => self.close(),
            SubmenuScreen::Search => return false,
        }
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::modules::launcher::files::FileEntry;

    fn lister(path: &str, dirs: bool) -> Vec<FileEntry> {
        vec![FileEntry { name: if dirs { "sub" } else { "file" }.into(), path: format!("{path}/x"), is_dir: dirs, score: 0.0 }]
    }

    #[test]
    fn launcher_submenu() {
        let mut s = SubmenuState::default();
        s.open_directory("/a/b", &lister);
        assert!(s.screen == SubmenuScreen::Browse && s.items.len() == 4, "browse lists actions, dirs and files");
        assert!(s.items[0].icon == SubmenuIcon::ArrowRight && s.items[1].icon == SubmenuIcon::ArrowLeft, "browse actions carry icons");
        assert_eq!(s.items[2].icon, SubmenuIcon::None, "listed entries have no icon");
        let item = |s: &SubmenuState, i: usize| s.items[i].clone();
        s.handle_entry(&item(&s, 1), &lister);
        assert_eq!(s.current_path, "/a", "previous directory goes up");
        s.open_directory("/", &lister);
        s.handle_entry(&item(&s, 1), &lister);
        assert_eq!(s.current_path, "/", "previous directory stops at the root");
        s.open_directory("/a/b", &lister);
        s.handle_entry(&item(&s, 2), &lister);
        assert!(s.screen == SubmenuScreen::Browse && s.current_path == "/a/b/x", "a directory opens");
        s.open_directory("/a/b", &lister);
        s.handle_entry(&item(&s, 3), &lister);
        assert!(s.screen == SubmenuScreen::FileActions && s.came_from_browse && s.items.len() == 2, "a file opens its actions");
        s.handle_entry(&item(&s, 1), &lister);
        assert!(s.screen == SubmenuScreen::DirActions && s.items.len() == 3, "containing directory opens directory actions");
        s.go_back(&lister);
        assert_eq!(s.screen, SubmenuScreen::Browse, "back from directory actions returns to browse");
        s.open_file_actions("/a/x");
        s.go_back(&lister);
        assert!(s.screen == SubmenuScreen::Browse && s.current_path == "/a", "back from file actions returns to its directory");
        s.go_back(&lister);
        assert!(s.screen == SubmenuScreen::Search && s.items.is_empty(), "back from browse closes");
        assert!(!s.go_back(&lister), "nothing to go back from search");
        s.open_file_actions("/other/file.txt");
        assert!(!s.came_from_browse, "a file opened from search does not come from browse");
        s.go_back(&lister);
        assert_eq!(s.screen, SubmenuScreen::Search, "back from search-opened file actions closes");
    }
}
