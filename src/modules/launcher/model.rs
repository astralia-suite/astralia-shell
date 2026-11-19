//! The launcher's state: the field, the mode, the results and the submenu, and the searches that fill them.

use std::cell::RefCell;
use std::collections::HashMap;
use std::rc::{Rc, Weak};
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{Duration, Instant};

use crate::base::animation::{AnimationManager, Easing, Value, value, writer};
use crate::base::input::{Clipboard, KeyEvent, KeyKind};
use crate::base::process::AsyncProcess;
use crate::config::launcher_config as cfg;
use crate::render::icons;
use crate::render::text_field::{
    CursorDir, TextCursor, TextFieldState, TextFieldTypeAnim, text_cursor_backspace, text_cursor_clamp, text_cursor_delete,
    text_cursor_delete_selection, text_cursor_end, text_cursor_home, text_cursor_insert, text_cursor_move, text_cursor_move_word,
    text_cursor_select_all, text_cursor_select_word, text_cursor_selected_text, text_cursor_set, text_field_byte_offset,
    text_field_char_index, text_field_idle_toggle, text_field_type_anim_clear, text_field_type_anim_settle, text_field_type_anim_sync,
    text_field_utf8_len,
};
use crate::service::runtime;

use super::apps::search_apps;
use super::desktop_entry::{DesktopEntry, desktop_entry_stamp, scan_desktop_entries};
use super::files::{FileEntry, collapse_home, fd_search_argv, fd_search_parse_output, list_directory, score_path, to_glob_pattern};
use super::index::{FileIndex, file_index_build, file_index_search};
use super::launch::{launch_app, launch_non_drun, launch_submenu_action};
use super::search::{DrunKind, DrunResult, LauncherMode, combined_drun_results, detect_mode_and_query};
use super::submenu::{SubmenuEntry, SubmenuIcon, SubmenuScreen, SubmenuState};
use super::visits::VisitStore;

const OWNER_HEIGHT: u64 = 100;
const OWNER_HIGHLIGHT: u64 = 101;
const OWNER_SCROLL: u64 = 102;
const OWNER_QUERY: u64 = 1000;

/// One list row as drawn.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Row {
    pub glyph: &'static str,
    pub label: String,
    pub subtitle: String,
    pub icon_path: String,
}

/// Work finished off the loop, applied by `Model::poll`. Each carries the generation it was started under.
pub enum Done {
    Debounce(u64),
    Index { generation: u64, index: Arc<FileIndex>, ms: u128 },
    Search { generation: u64, files: Vec<FileEntry> },
    Fd { generation: u64, dirs: bool, output: Option<String> },
    Paste { session: u64, text: String },
}

/// Where finished work waits for the loop; `wake` tells the host to poll.
#[derive(Default)]
pub struct Inbox {
    done: RefCell<Vec<Done>>,
    wake: RefCell<Option<Rc<dyn Fn()>>>,
}

impl Inbox {
    pub fn push(&self, done: Done) {
        self.done.borrow_mut().push(done);
        let wake = self.wake.borrow().clone();
        if let Some(wake) = wake {
            wake();
        }
    }
}

fn post(inbox: &Weak<Inbox>, done: Done) {
    if let Some(inbox) = inbox.upgrade() {
        inbox.push(done);
    }
}

fn list_dir(path: &str, want_dirs: bool) -> Vec<FileEntry> {
    list_directory(path, want_dirs)
}

fn submenu_glyph(entry: &SubmenuEntry) -> &'static str {
    match entry.icon {
        SubmenuIcon::ArrowRight => icons::ARROW_RIGHT,
        SubmenuIcon::ArrowLeft => icons::ARROW_LEFT,
        SubmenuIcon::Code => icons::CODE,
        SubmenuIcon::Terminal => icons::TERMINAL,
        SubmenuIcon::FolderOpen => icons::FOLDER_OPEN,
        SubmenuIcon::None if entry.is_dir => icons::FOLDER,
        SubmenuIcon::None => icons::EDIT,
    }
}

fn home() -> Option<String> {
    std::env::var("HOME").ok()
}

/// The card's height with `visible_rows` rows under the field.
pub fn content_height(visible_rows: usize) -> f32 {
    let mut height = cfg::MENU_PAD * 2.0 + cfg::SEARCH_HEIGHT;
    if visible_rows > 0 {
        let rows = visible_rows as f32;
        height += cfg::LIST_GAP + rows * cfg::ROW_HEIGHT + (rows - 1.0) * cfg::ROW_SPACING;
    }
    height
}

pub struct Model {
    animations: AnimationManager,
    inbox: Rc<Inbox>,
    /// Bumped on every close, so a paste that arrives later is dropped.
    session: u64,
    /// The file search program; tests point it elsewhere.
    pub(crate) fd_program: &'static str,

    open: bool,
    search_root: String,
    apps: Vec<DesktopEntry>,
    apps_stamp: Vec<i64>,
    apps_loaded: bool,
    field: TextFieldState,
    cursor: TextCursor,
    clipboard: Option<Rc<dyn Clipboard>>,
    field_first: usize,
    dragging: bool,
    last_click: Option<(Instant, usize)>,
    query_anim: TextFieldTypeAnim,
    mode: LauncherMode,
    effective_query: String,
    search_query: String,
    results: Vec<DrunResult>,
    submenu: SubmenuState,
    visits: VisitStore,
    selected: Option<usize>,
    hovered: Option<usize>,
    icon_paths: HashMap<String, String>,
    close_requested: bool,

    debounce_pending: bool,
    debounce_generation: u64,
    dirs_proc: AsyncProcess,
    files_proc: AsyncProcess,
    dirs_output: String,
    files_output: String,
    pending_searches: u32,
    searching: bool,
    search_generation: u64,

    index: Option<Arc<FileIndex>>,
    index_cancel: Option<Arc<AtomicBool>>,
    index_root: String,
    index_built: Option<Instant>,
    index_generation: u64,
    indexing: bool,

    height: Value,
    height_target: f32,
    highlight: Value,
    highlight_target: f32,
    scroll: Value,
    scroll_target: f32,

    /// Called after every change the card shows.
    pub on_changed: Option<Box<dyn FnMut()>>,
}

impl Default for Model {
    fn default() -> Model {
        Model {
            animations: AnimationManager::default(),
            inbox: Rc::default(),
            session: 0,
            fd_program: "fd",
            open: false,
            search_root: String::new(),
            apps: Vec::new(),
            apps_stamp: Vec::new(),
            apps_loaded: false,
            field: TextFieldState::default(),
            cursor: TextCursor::default(),
            clipboard: None,
            field_first: 0,
            dragging: false,
            last_click: None,
            query_anim: TextFieldTypeAnim::default(),
            mode: LauncherMode::Drun,
            effective_query: String::new(),
            search_query: String::new(),
            results: Vec::new(),
            submenu: SubmenuState::default(),
            visits: VisitStore::default(),
            selected: None,
            hovered: None,
            icon_paths: HashMap::new(),
            close_requested: false,
            debounce_pending: false,
            debounce_generation: 0,
            dirs_proc: AsyncProcess::default(),
            files_proc: AsyncProcess::default(),
            dirs_output: String::new(),
            files_output: String::new(),
            pending_searches: 0,
            searching: false,
            search_generation: 0,
            index: None,
            index_cancel: None,
            index_root: String::new(),
            index_built: None,
            index_generation: 0,
            indexing: false,
            height: value(0.0),
            height_target: -1.0,
            highlight: value(0.0),
            highlight_target: -1.0,
            scroll: value(0.0),
            scroll_target: -1.0,
            on_changed: None,
        }
    }
}

impl Drop for Model {
    fn drop(&mut self) {
        if let Some(cancel) = &self.index_cancel {
            cancel.store(true, Ordering::Relaxed);
        }
        self.stop_search();
    }
}

impl Model {
    /// Called whenever finished work waits in the inbox.
    pub fn set_wake(&mut self, wake: Rc<dyn Fn()>) {
        *self.inbox.wake.borrow_mut() = Some(wake);
    }

    pub fn set_clipboard(&mut self, clipboard: Option<Rc<dyn Clipboard>>) {
        self.clipboard = clipboard;
    }

    #[cfg(test)]
    pub fn set_apps(&mut self, apps: Vec<DesktopEntry>) {
        self.apps = apps;
    }

    pub fn is_open(&self) -> bool {
        self.open
    }

    #[cfg(test)]
    pub fn searching(&self) -> bool {
        self.searching
    }

    #[cfg(test)]
    pub fn indexing(&self) -> bool {
        self.indexing
    }

    pub fn animating(&self) -> bool {
        self.animations.has_active()
    }

    pub fn mode(&self) -> LauncherMode {
        self.mode
    }

    pub fn field(&self) -> &TextFieldState {
        &self.field
    }

    pub fn cursor(&self) -> &TextCursor {
        &self.cursor
    }

    pub fn query_anim(&self) -> &TextFieldTypeAnim {
        &self.query_anim
    }

    #[cfg(test)]
    pub fn results(&self) -> &[DrunResult] {
        &self.results
    }

    #[cfg(test)]
    pub fn submenu(&self) -> &SubmenuState {
        &self.submenu
    }

    pub fn selected(&self) -> Option<usize> {
        self.selected
    }

    pub fn hovered(&self) -> Option<usize> {
        self.hovered
    }

    pub fn box_height(&self) -> f32 {
        self.height.get()
    }

    pub fn highlight_offset(&self) -> f32 {
        self.highlight.get()
    }

    pub fn scroll_offset(&self) -> f32 {
        self.scroll.get()
    }

    pub fn tick(&mut self, now: Instant) {
        self.animations.tick(now);
    }

    /// True once after a key or click asked the launcher to close.
    pub fn take_close_request(&mut self) -> bool {
        std::mem::take(&mut self.close_requested)
    }

    pub fn clear_preedit(&mut self) {
        self.field.preedit.clear();
    }

    fn changed(&mut self) {
        if let Some(on_changed) = &mut self.on_changed {
            on_changed();
        }
    }

    /// Searches from home, or from `/` when `global`; reloads the apps when their files changed.
    pub fn open(&mut self, global: bool) {
        let home = home();
        self.search_root = match &home {
            Some(home) if !global => home.clone(),
            _ => "/".into(),
        };
        let stamp = desktop_entry_stamp();
        if !self.apps_loaded || stamp != self.apps_stamp {
            self.apps = scan_desktop_entries();
            self.apps_stamp = stamp;
            self.apps_loaded = true;
        }
        self.visits = VisitStore::load_default();
        self.open = true;
        self.field.cursor_idle_visible = true;
        self.start_index();
        ::log::info!("launcher: open, searching from {}", self.search_root);
    }

    #[cfg(test)]
    pub fn set_search_root(&mut self, root: &str) {
        self.search_root = root.to_string();
        self.start_index();
    }

    fn start_index(&mut self) {
        if self.search_root == "/" || (self.indexing && self.index_root == self.search_root) {
            return;
        }
        let fresh = self.index_built.is_some_and(|built| built.elapsed() < Duration::from_secs(cfg::INDEX_REFRESH_S as u64));
        if !self.indexing && self.index.as_ref().is_some_and(|i| i.root == self.search_root) && fresh {
            return;
        }
        if let Some(cancel) = &self.index_cancel {
            cancel.store(true, Ordering::Relaxed);
        }
        let cancel = Arc::new(AtomicBool::new(false));
        self.index_cancel = Some(cancel.clone());
        self.index_root = self.search_root.clone();
        self.indexing = true;
        self.index_generation += 1;
        let (generation, root, inbox) = (self.index_generation, self.index_root.clone(), Rc::downgrade(&self.inbox));
        runtime::spawn(async move {
            let started = Instant::now();
            let built = blocking::unblock(move || file_index_build(&root, &cancel)).await;
            post(&inbox, Done::Index { generation, index: Arc::new(built), ms: started.elapsed().as_millis() });
        });
    }

    pub fn close(&mut self) {
        self.stop_search();
        self.debounce_pending = false;
        self.session += 1;
        self.open = false;
        self.field.text.clear();
        self.field.preedit.clear();
        self.cursor = TextCursor::default();
        self.field_first = 0;
        self.dragging = false;
        self.last_click = None;
        text_field_type_anim_clear(&mut self.query_anim, &mut self.animations, OWNER_QUERY);
        self.mode = LauncherMode::Drun;
        self.effective_query.clear();
        self.search_query.clear();
        self.results.clear();
        self.submenu.close();
        self.selected = None;
        self.hovered = None;
        self.close_requested = false;
        (self.height_target, self.highlight_target, self.scroll_target) = (-1.0, -1.0, -1.0);
        for owner in [OWNER_HEIGHT, OWNER_HIGHLIGHT, OWNER_SCROLL] {
            self.animations.cancel_owner(owner);
        }
    }

    pub fn item_count(&self) -> usize {
        if self.submenu.screen == SubmenuScreen::Search { self.results.len() } else { self.submenu.items.len() }
    }

    pub fn first_visible(&self) -> usize {
        let max = cfg::MAX_VISIBLE as usize;
        match self.selected {
            Some(s) if s >= max => s - max + 1,
            _ => 0,
        }
    }

    fn reset_selection(&mut self) {
        self.selected = if self.item_count() == 0 { None } else { Some(0) };
        self.hovered = None;
    }

    fn query_changed(&mut self) {
        let mq = detect_mode_and_query(&self.field.text);
        self.mode = mq.mode;
        self.effective_query = mq.query;
        self.field.cursor_idle_visible = true;
        text_cursor_clamp(&self.field.text, &mut self.cursor);
        if self.cursor.pos == self.field.text.len() {
            text_field_type_anim_sync(&mut self.query_anim, &mut self.animations, OWNER_QUERY, &self.field.text);
        } else {
            text_field_type_anim_settle(&mut self.query_anim, &mut self.animations, OWNER_QUERY, &self.field.text);
        }
        self.arm_debounce();
    }

    fn text_edited(&mut self) {
        if self.submenu.screen != SubmenuScreen::Search {
            self.submenu.close();
        }
        self.field.preedit.clear();
        self.query_changed();
    }

    fn paste(&mut self, text: &str) {
        let line: String = text
            .chars()
            .map(|c| if matches!(c, '\n' | '\r' | '\t') { ' ' } else { c })
            .filter(|&c| c >= ' ' && c != '\u{7f}')
            .collect();
        if line.is_empty() {
            return;
        }
        self.hovered = None;
        text_cursor_insert(&mut self.field.text, &mut self.cursor, &line);
        self.text_edited();
        self.changed();
    }

    fn key_text(&mut self, event: &KeyEvent) {
        if event.alt {
            return;
        }
        if !event.ctrl {
            if event.text.is_empty() {
                return;
            }
            text_cursor_insert(&mut self.field.text, &mut self.cursor, &event.text);
            self.text_edited();
            return;
        }
        match char::from_u32(event.base_sym).map(|c| c.to_ascii_lowercase()) {
            Some('a') => {
                text_cursor_select_all(&self.field.text, &mut self.cursor);
                self.field.cursor_idle_visible = true;
            }
            Some(sym @ ('c' | 'x')) => {
                if self.cursor.has_selection()
                    && let Some(clipboard) = &self.clipboard
                {
                    clipboard.set(text_cursor_selected_text(&self.field.text, &self.cursor));
                }
                if sym == 'x' && text_cursor_delete_selection(&mut self.field.text, &mut self.cursor) {
                    self.text_edited();
                }
            }
            Some('v') => {
                if let Some(clipboard) = &self.clipboard {
                    let (inbox, session) = (Rc::downgrade(&self.inbox), self.session);
                    clipboard.request(Box::new(move |text| post(&inbox, Done::Paste { session, text })));
                }
            }
            _ => {}
        }
    }

    fn byte_at_column(&self, column: f32) -> usize {
        text_field_byte_offset(&self.field.text, (column.max(0.0) + 0.5) as usize)
    }

    /// The first character shown in a field `width` characters wide, scrolled to keep the caret in view.
    pub fn field_window(&mut self, width: usize) -> usize {
        let total = text_field_utf8_len(&self.field.text);
        if total <= width {
            self.field_first = 0;
            return 0;
        }
        let column = text_field_char_index(&self.field.text, self.cursor.pos);
        if column < self.field_first {
            self.field_first = column;
        } else if column > self.field_first + width {
            self.field_first = column - width;
        }
        self.field_first = self.field_first.min(total - width);
        self.field_first
    }

    /// A press on the field at a character `column`; a second press on the same spot soon after selects the word.
    pub fn pointer_down(&mut self, column: f32, now: Instant) {
        if !self.open {
            return;
        }
        let byte = self.byte_at_column(column);
        let double_click = self
            .last_click
            .is_some_and(|(at, at_byte)| at_byte == byte && now.saturating_duration_since(at) <= Duration::from_millis(cfg::DOUBLE_CLICK_MS as u64));
        self.field.cursor_idle_visible = true;
        self.hovered = None;
        if double_click {
            let word = text_field_byte_offset(&self.field.text, column.max(0.0) as usize);
            text_cursor_select_word(&self.field.text, &mut self.cursor, word);
            self.last_click = None;
            self.dragging = false;
        } else {
            text_cursor_set(&self.field.text, &mut self.cursor, byte, false);
            self.last_click = Some((now, byte));
            self.dragging = true;
        }
        self.changed();
    }

    pub fn pointer_drag(&mut self, column: f32) {
        if !self.open || !self.dragging {
            return;
        }
        let byte = self.byte_at_column(column);
        text_cursor_set(&self.field.text, &mut self.cursor, byte, true);
        self.field.cursor_idle_visible = true;
        self.changed();
    }

    pub fn dragging(&self) -> bool {
        self.dragging
    }

    pub fn pointer_up(&mut self) {
        self.dragging = false;
    }

    fn arm_debounce(&mut self) {
        self.debounce_pending = true;
        self.debounce_generation += 1;
        let (generation, inbox) = (self.debounce_generation, Rc::downgrade(&self.inbox));
        runtime::spawn(async move {
            async_io::Timer::after(Duration::from_millis(cfg::DEBOUNCE_MS as u64)).await;
            post(&inbox, Done::Debounce(generation));
        });
    }

    fn debounce_fired(&mut self) {
        if !self.debounce_pending {
            return;
        }
        self.debounce_pending = false;
        if !self.open {
            return;
        }
        if self.mode == LauncherMode::Drun && !self.effective_query.is_empty() {
            self.start_search();
            return;
        }
        self.stop_search();
        self.results.clear();
        if self.submenu.screen == SubmenuScreen::Search {
            self.selected = None;
        }
        self.changed();
    }

    fn start_search(&mut self) {
        self.stop_search();
        self.search_query = self.effective_query.clone();
        let (generation, inbox) = (self.search_generation, Rc::downgrade(&self.inbox));
        if let Some(index) = self.index.clone().filter(|i| i.root == self.search_root) {
            self.searching = true;
            let (query, visits) = (self.search_query.clone(), self.visits.clone());
            runtime::spawn(async move {
                let files = blocking::unblock(move || file_index_search(&index, &query, &visits, cfg::MAX_RESULTS as usize)).await;
                post(&inbox, Done::Search { generation, files });
            });
            return;
        }
        let pattern = to_glob_pattern(&self.search_query);
        self.searching = true;
        self.pending_searches = 2;
        for dirs in [true, false] {
            let mut argv = fd_search_argv(&pattern, &self.search_root, dirs, cfg::MAX_RESULTS, -1, true);
            argv[0] = self.fd_program.to_string();
            let argv: Vec<&str> = argv.iter().map(String::as_str).collect();
            let process = if dirs { &mut self.dirs_proc } else { &mut self.files_proc };
            let run = process.start(&argv, false);
            let inbox = inbox.clone();
            runtime::spawn(async move {
                let output = run.await;
                post(&inbox, Done::Fd { generation, dirs, output });
            });
        }
    }

    fn stop_search(&mut self) {
        self.dirs_proc.cancel();
        self.files_proc.cancel();
        self.dirs_output.clear();
        self.files_output.clear();
        self.pending_searches = 0;
        self.searching = false;
        self.search_generation += 1;
    }

    fn finish_search(&mut self) {
        self.searching = false;
        let mut files = Vec::new();
        for dirs in [true, false] {
            let output = if dirs { &self.dirs_output } else { &self.files_output };
            for mut file in fd_search_parse_output(output, dirs) {
                file.score = score_path(&file.name, &self.search_query);
                if file.score >= 0.0 {
                    files.push(file);
                }
            }
        }
        self.publish(&files);
    }

    fn publish(&mut self, files: &[FileEntry]) {
        let apps = search_apps(&self.apps, &self.search_query);
        self.results = combined_drun_results(&self.apps, &apps, files, &self.visits, cfg::MAX_RESULTS as usize);
        if self.submenu.screen == SubmenuScreen::Search {
            self.selected = if self.results.is_empty() { None } else { Some(0) };
        }
        self.changed();
    }

    /// Applies the work finished since the last call; true when any arrived.
    pub fn poll(&mut self) -> bool {
        let done = std::mem::take(&mut *self.inbox.done.borrow_mut());
        let any = !done.is_empty();
        for done in done {
            match done {
                Done::Debounce(generation) if generation == self.debounce_generation => self.debounce_fired(),
                Done::Index { generation, index, ms } if generation == self.index_generation => {
                    self.indexing = false;
                    ::log::info!("launcher: indexed {} entries under {} in {ms}ms", index.nodes.len(), index.root);
                    self.index = Some(index);
                    self.index_built = Some(Instant::now());
                }
                Done::Search { generation, files } if generation == self.search_generation && self.searching => {
                    self.searching = false;
                    self.publish(&files);
                }
                Done::Fd { generation, dirs, output } if generation == self.search_generation && self.searching => {
                    *(if dirs { &mut self.dirs_output } else { &mut self.files_output }) = output.unwrap_or_default();
                    self.pending_searches = self.pending_searches.saturating_sub(1);
                    if self.pending_searches == 0 {
                        self.finish_search();
                    }
                }
                Done::Paste { session, text } if session == self.session && self.open => self.paste(&text),
                _ => {}
            }
        }
        any
    }

    fn launch_selected(&mut self) {
        if self.submenu.screen != SubmenuScreen::Search {
            let Some(entry) = self.selected.and_then(|s| self.submenu.items.get(s)).cloned() else { return };
            if self.submenu.handle_entry(&entry, &list_dir) {
                self.reset_selection();
                return;
            }
            launch_submenu_action(&entry, &mut self.visits);
            self.close_requested = true;
            return;
        }
        if self.mode != LauncherMode::Drun {
            launch_non_drun(self.mode, &self.effective_query);
            self.close_requested = true;
            return;
        }
        let Some(result) = self.selected.and_then(|s| self.results.get(s)).cloned() else { return };
        match result.kind {
            DrunKind::App => {
                if let Some(entry) = result.app.and_then(|i| self.apps.get(i)) {
                    launch_app(entry, &mut self.visits);
                }
                self.close_requested = true;
            }
            DrunKind::Dir => {
                self.submenu.open_directory(&result.file.path, &list_dir);
                self.reset_selection();
            }
            DrunKind::File => {
                self.submenu.open_file_actions(&result.file.path);
                self.reset_selection();
            }
        }
    }

    pub fn key(&mut self, event: &KeyEvent) {
        self.hovered = None;
        match event.kind {
            KeyKind::Text => self.key_text(event),
            KeyKind::Preedit => {
                self.field.preedit.clone_from(&event.text);
                self.field.cursor_idle_visible = true;
            }
            KeyKind::Backspace => {
                text_cursor_backspace(&mut self.field.text, &mut self.cursor);
                self.text_edited();
            }
            KeyKind::Delete => {
                if text_cursor_delete(&mut self.field.text, &mut self.cursor) {
                    self.text_edited();
                }
            }
            KeyKind::Left | KeyKind::Right => {
                let dir = if event.kind == KeyKind::Left { CursorDir::Left } else { CursorDir::Right };
                if event.ctrl {
                    text_cursor_move_word(&self.field.text, &mut self.cursor, dir, event.shift);
                } else {
                    text_cursor_move(&self.field.text, &mut self.cursor, dir, event.shift);
                }
                self.field.cursor_idle_visible = true;
            }
            KeyKind::Home => {
                text_cursor_home(&mut self.cursor, event.shift);
                self.field.cursor_idle_visible = true;
            }
            KeyKind::End => {
                text_cursor_end(&self.field.text, &mut self.cursor, event.shift);
                self.field.cursor_idle_visible = true;
            }
            KeyKind::Up | KeyKind::Down => {
                let count = self.item_count() as i64;
                let step = if event.kind == KeyKind::Down { 1 } else { -1 };
                let from = self.selected.map_or(-1, |s| s as i64);
                self.selected = if count == 0 { None } else { Some((from + step).clamp(0, count - 1) as usize) };
            }
            KeyKind::Escape => {
                if self.submenu.screen == SubmenuScreen::Search {
                    self.close_requested = true;
                    self.poll();
                    return;
                }
                self.submenu.go_back(&list_dir);
                self.reset_selection();
            }
            KeyKind::Enter => self.launch_selected(),
            KeyKind::Tab => {}
        }
        self.changed();
        // A clipboard that answers at once has queued its paste.
        self.poll();
    }

    pub fn commit_text(&mut self, text: &str) {
        text_cursor_insert(&mut self.field.text, &mut self.cursor, text);
        self.field.preedit.clear();
        self.query_changed();
        self.changed();
    }

    pub fn delete_before(&mut self, count: u32) {
        text_cursor_clamp(&self.field.text, &mut self.cursor);
        self.cursor.anchor = self.cursor.pos;
        for _ in 0..count {
            text_cursor_backspace(&mut self.field.text, &mut self.cursor);
        }
        self.query_changed();
        self.changed();
    }

    pub fn set_preedit(&mut self, text: &str) {
        self.field.preedit = text.to_string();
        self.field.cursor_idle_visible = true;
        self.changed();
    }

    pub fn click_row(&mut self, index: usize) {
        self.selected = Some(index);
        self.hovered = None;
        self.launch_selected();
        self.changed();
    }

    /// The row under the pointer; true when it changed.
    pub fn hover(&mut self, row: Option<usize>) -> bool {
        let next = row.filter(|_| self.open);
        if next == self.hovered {
            return false;
        }
        self.hovered = next;
        true
    }

    pub fn toggle_caret(&mut self) {
        text_field_idle_toggle(&mut self.field);
    }

    fn icon_path_for(&mut self, app: usize) -> String {
        let entry = &self.apps[app];
        self.icon_paths.entry(entry.id.clone()).or_insert_with(|| crate::service::icon::app_icon_path(&entry.icon)).clone()
    }

    pub fn rows(&mut self) -> Vec<Row> {
        let home = home().unwrap_or_default();
        if self.submenu.screen == SubmenuScreen::Search {
            let results = self.results.clone();
            return results
                .iter()
                .map(|result| match result.kind {
                    DrunKind::App => {
                        let app = result.app.unwrap_or_default();
                        let label = self.apps.get(app).map(|a| a.name.clone()).unwrap_or_default();
                        let icon_path = if app < self.apps.len() { self.icon_path_for(app) } else { String::new() };
                        Row { glyph: icons::APPS, label, subtitle: String::new(), icon_path }
                    }
                    DrunKind::Dir | DrunKind::File => Row {
                        glyph: if result.kind == DrunKind::Dir { icons::FOLDER } else { icons::EDIT },
                        label: result.file.name.clone(),
                        subtitle: collapse_home(&result.file.path, &home),
                        icon_path: String::new(),
                    },
                })
                .collect();
        }
        self.submenu
            .items
            .iter()
            .map(|entry| Row {
                glyph: submenu_glyph(entry),
                label: entry.name.clone(),
                subtitle: if entry.path.is_empty() { String::new() } else { collapse_home(&entry.path, &home) },
                icon_path: String::new(),
            })
            .collect()
    }

    /// Moves the card height, the highlight and the scroll towards the current list.
    pub fn sync_layout(&mut self) {
        let visible = self.item_count().min(cfg::MAX_VISIBLE as usize);
        let content = content_height(visible);
        if self.height_target < 0.0 {
            self.height_target = content;
            self.height.set(content);
        } else if content != self.height_target {
            self.height_target = content;
            self.animations.animate(self.height.get(), content, cfg::HEIGHT_ANIM_MS, Easing::EaseInOutCubic, writer(&self.height), None, OWNER_HEIGHT);
        }
        let Some(selected) = self.selected else {
            (self.highlight_target, self.scroll_target) = (-1.0, -1.0);
            return;
        };
        let highlight = selected as f32 * cfg::ROW_PITCH;
        if self.highlight_target < 0.0 {
            self.highlight_target = highlight;
            self.highlight.set(highlight);
        } else if highlight != self.highlight_target {
            self.highlight_target = highlight;
            self.animations.animate(self.highlight.get(), highlight, cfg::HIGHLIGHT_ANIM_MS, Easing::EaseOutCubic, writer(&self.highlight), None, OWNER_HIGHLIGHT);
        }
        let scroll = self.first_visible() as f32 * cfg::ROW_PITCH;
        if self.scroll_target < 0.0 {
            self.scroll_target = scroll;
            self.scroll.set(scroll);
        } else if scroll != self.scroll_target {
            self.scroll_target = scroll;
            self.animations.animate(self.scroll.get(), scroll, cfg::HIGHLIGHT_ANIM_MS, Easing::EaseOutCubic, writer(&self.scroll), None, OWNER_SCROLL);
        }
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use crate::base::animation::set_instant;
    use crate::base::input::{Clipboard, KeyEvent, KeyKind};
    use crate::config::launcher_config as cfg;
    use crate::modules::launcher::desktop_entry::DesktopEntry;
    use crate::modules::launcher::search::{DrunKind, LauncherMode};
    use crate::modules::launcher::submenu::SubmenuScreen;
    use crate::render::text_field::text_cursor_selected_text;
    use calloop::EventLoop;
    use std::cell::RefCell;
    use std::rc::Rc;
    use std::time::{Duration, Instant};

    pub struct Snap;

    impl Snap {
        pub fn on() -> Snap {
            set_instant(true);
            Snap
        }
    }

    impl Drop for Snap {
        fn drop(&mut self) {
            set_instant(false);
        }
    }

    /// Runs this thread's loop, so spawned searches, timers and index builds finish.
    struct Pump(EventLoop<'static, ()>);

    impl Pump {
        fn new() -> Pump {
            let event_loop = EventLoop::try_new().unwrap();
            crate::service::runtime::attach(&event_loop.handle()).unwrap();
            Pump(event_loop)
        }

        fn until(&mut self, model: &mut Model, done: impl Fn(&Model) -> bool) -> bool {
            let deadline = Instant::now() + Duration::from_secs(10);
            while Instant::now() < deadline {
                self.0.dispatch(Some(Duration::from_millis(5)), &mut ()).unwrap();
                model.poll();
                if done(model) {
                    return true;
                }
            }
            false
        }
    }

    pub fn key(kind: KeyKind, text: &str) -> KeyEvent {
        KeyEvent { kind, text: text.to_string(), ..KeyEvent::default() }
    }

    pub fn type_text(model: &mut Model, text: &str) {
        for c in text.chars() {
            model.key(&key(KeyKind::Text, &c.to_string()));
        }
    }

    fn chord(kind: KeyKind, shift: bool, ctrl: bool) -> KeyEvent {
        KeyEvent { kind, shift, ctrl, ..KeyEvent::default() }
    }

    fn shortcut(letter: char) -> KeyEvent {
        let text = char::from(letter as u8 - b'a' + 1).to_string();
        KeyEvent { kind: KeyKind::Text, ctrl: true, base_sym: letter as u32, text, ..KeyEvent::default() }
    }

    fn firefox() -> Vec<DesktopEntry> {
        vec![DesktopEntry { id: "firefox.desktop".into(), name: "Firefox".into(), exec: "firefox".into(), ..DesktopEntry::default() }]
    }

    fn selected(model: &Model) -> String {
        text_cursor_selected_text(&model.field().text, model.cursor())
    }

    fn fd_installed() -> bool {
        std::process::Command::new("sh").args(["-c", "command -v fd >/dev/null 2>&1"]).status().is_ok_and(|s| s.success())
    }

    fn fixture(name: &str) -> tempfile::TempDir {
        let root = tempfile::Builder::new().prefix(name).tempdir().unwrap();
        std::fs::create_dir_all(root.path().join("firefoxdir")).unwrap();
        std::fs::write(root.path().join("firefox_notes.txt"), "x").unwrap();
        root
    }

    type Paste = Box<dyn FnOnce(String)>;

    #[derive(Default)]
    struct FakeClipboard {
        stored: RefCell<String>,
        defer: std::cell::Cell<bool>,
        waiting: RefCell<Option<Paste>>,
    }

    impl Clipboard for FakeClipboard {
        fn set(&self, text: String) {
            *self.stored.borrow_mut() = text;
        }
        fn request(&self, done: Box<dyn FnOnce(String)>) {
            if self.defer.get() {
                *self.waiting.borrow_mut() = Some(done);
            } else {
                done(self.stored.borrow().clone());
            }
        }
    }

    #[test]
    fn launcher_modes_and_layout() {
        let _snap = Snap::on();
        let mut model = Model::default();
        model.open(false);
        let changes = Rc::new(std::cell::Cell::new(0));
        let counter = changes.clone();
        model.on_changed = Some(Box::new(move || counter.set(counter.get() + 1)));
        model.set_search_root("/nonexistent-root");
        model.set_apps(firefox());

        type_text(&mut model, "gg cats");
        assert!(model.mode() == LauncherMode::Google && model.field().text == "gg cats", "a prefix switches the mode");
        assert_eq!(changes.get(), 7, "each key reports a change");
        model.key(&key(KeyKind::Backspace, ""));
        model.key(&key(KeyKind::Backspace, ""));
        assert_eq!(model.field().text, "gg ca", "backspace removes whole characters");
        assert_eq!(model.query_anim().chars.len(), 5, "the typing animation follows the text");

        model.hover(Some(2));
        model.key(&key(KeyKind::Up, ""));
        assert_eq!(model.hovered(), None, "a key press clears the hover");

        model.sync_layout();
        assert_eq!(model.box_height(), content_height(0), "an empty list shows only the search row");
        assert_eq!(
            content_height(6),
            cfg::MENU_PAD * 2.0 + cfg::SEARCH_HEIGHT + cfg::LIST_GAP + 6.0 * cfg::ROW_HEIGHT + 5.0 * cfg::ROW_SPACING,
            "full list height"
        );

        model.set_preedit("ni");
        assert_eq!(model.field().preedit, "ni", "preedit is kept apart from the text");
        model.commit_text("x");
        assert!(model.field().text == "gg cax" && model.field().preedit.is_empty(), "committing appends and drops the preedit");
        model.delete_before(2);
        assert_eq!(model.field().text, "gg c", "deleting before the cursor removes characters");
        model.toggle_caret();
        assert!(!model.field().cursor_idle_visible, "the caret blink toggles");
        model.key(&key(KeyKind::Text, "a"));
        assert!(model.field().cursor_idle_visible, "typing shows the caret");

        model.key(&key(KeyKind::Escape, ""));
        assert!(model.take_close_request(), "escape on the main screen asks to close");
        model.key(&key(KeyKind::Left, ""));
        model.key(&key(KeyKind::Right, ""));
        assert!(!model.take_close_request(), "other keys do nothing");
    }

    #[test]
    fn launcher_search_flow() {
        if !fd_installed() {
            return;
        }
        let _snap = Snap::on();
        let mut pump = Pump::new();
        let root = fixture("astralia_test_model_");
        let mut model = Model::default();
        model.open(false);
        model.set_search_root(root.path().to_str().unwrap());
        // The index would answer instead of `fd` once built; dropping its result keeps this on `fd`.
        model.index_generation += 1;
        model.set_apps(firefox());
        let changed = Rc::new(std::cell::Cell::new(false));
        let flag = changed.clone();
        model.on_changed = Some(Box::new(move || flag.set(true)));

        type_text(&mut model, "firefox");
        changed.set(false);
        assert!(pump.until(&mut model, |m| !m.searching() && m.results().len() == 3), "the search finishes");
        assert!(changed.get(), "finishing the search reports a change");
        let results = model.results();
        assert!(results.len() == 3 && results[0].kind == DrunKind::App, "the app comes first");
        assert!(results[1].kind == DrunKind::Dir && results[1].file.name == "firefoxdir", "then the directory");
        assert!(results[2].kind == DrunKind::File && results[2].file.name == "firefox_notes.txt", "then the file");
        assert_eq!(model.selected(), Some(0), "the first result is selected");

        let rows = model.rows();
        assert!(rows.len() == 3 && rows[0].label == "Firefox" && rows[1].subtitle.contains("firefoxdir"), "rows describe the results");

        model.key(&key(KeyKind::Down, ""));
        model.key(&key(KeyKind::Down, ""));
        model.key(&key(KeyKind::Down, ""));
        assert_eq!(model.selected(), Some(2), "selection stops at the last row");
        model.key(&key(KeyKind::Up, ""));
        assert_eq!(model.selected(), Some(1), "up moves back");
        model.key(&key(KeyKind::Enter, ""));
        assert!(model.submenu().screen == SubmenuScreen::Browse && model.selected() == Some(0), "enter on a directory opens it");
        assert!(!model.submenu().items.is_empty(), "the directory listing has entries");
        model.key(&key(KeyKind::Escape, ""));
        assert!(model.submenu().screen == SubmenuScreen::Search && model.selected() == Some(0), "escape goes back to the results");
        model.click_row(2);
        assert!(model.submenu().screen == SubmenuScreen::FileActions && model.selected() == Some(0), "clicking a file opens its actions");
        model.key(&key(KeyKind::Text, "z"));
        assert_eq!(model.submenu().screen, SubmenuScreen::Search, "typing leaves the submenu");

        for _ in 0..8 {
            model.key(&key(KeyKind::Backspace, ""));
        }
        assert!(pump.until(&mut model, |m| m.results().is_empty()), "an empty query clears the results");
        assert_eq!(model.selected(), None, "and the selection");

        model.close();
        assert!(
            !model.is_open() && model.field().text.is_empty() && model.results().is_empty() && model.submenu().screen == SubmenuScreen::Search,
            "closing resets everything"
        );
    }

    #[test]
    fn launcher_index_search_flow() {
        let _snap = Snap::on();
        let mut pump = Pump::new();
        let root = fixture("astralia_test_model_index_");
        let mut model = Model::default();
        model.open(false);
        model.set_search_root(root.path().to_str().unwrap());
        model.set_apps(firefox());
        assert!(pump.until(&mut model, |m| !m.indexing()), "the index finishes");

        type_text(&mut model, "firefox");
        assert!(pump.until(&mut model, |m| m.results().len() == 3), "the indexed search answers");
        assert!(!model.searching(), "the search is done");
        let results = model.results();
        let dir = format!("{}/", root.path().join("firefoxdir").display());
        assert!(
            results[0].kind == DrunKind::App
                && results[1].kind == DrunKind::Dir
                && results[1].file.name == "firefoxdir"
                && results[1].file.path == dir
                && results[2].kind == DrunKind::File
                && results[2].file.name == "firefox_notes.txt",
            "apps, then folders, then files"
        );

        for _ in 0..7 {
            model.key(&key(KeyKind::Backspace, ""));
        }
        assert!(pump.until(&mut model, |m| m.results().is_empty()), "an empty query shows no list");
        model.close();
    }

    #[test]
    fn search_without_fd() {
        let _snap = Snap::on();
        let mut pump = Pump::new();
        let mut model = Model::default();
        model.fd_program = "/nonexistent/fd";
        model.open(false);
        model.set_search_root("/nonexistent-root");
        model.set_apps(firefox());
        type_text(&mut model, "fire");
        assert!(pump.until(&mut model, |m| !m.searching() && !m.results().is_empty()), "the search ends without fd");
        let results = model.results();
        assert!(results.len() == 1 && results[0].kind == DrunKind::App, "only the app row shows: {results:?}");

        // A result from an older search is dropped.
        let generation = model.search_generation;
        model.inbox.push(Done::Fd { generation: generation.wrapping_sub(1), dirs: false, output: Some("/tmp/fire.txt\n".into()) });
        model.poll();
        assert_eq!(model.results().len(), 1, "a stale fd answer changes nothing");
        model.close();
    }

    #[test]
    fn launcher_layout_animation() {
        let _snap = Snap::on();
        let mut model = Model::default();
        model.sync_layout();
        assert_eq!(model.box_height(), content_height(0), "starts at the search row");
        assert!(model.highlight_offset() == 0.0 && model.scroll_offset() == 0.0, "nothing selected, nothing offset");
    }

    #[test]
    fn launcher_caret_editing() {
        let _snap = Snap::on();
        let mut model = Model::default();
        model.open(false);
        model.set_search_root("/nonexistent-root");
        model.set_apps(Vec::new());

        type_text(&mut model, "hello");
        model.key(&key(KeyKind::Left, ""));
        model.key(&key(KeyKind::Left, ""));
        assert!(model.cursor().pos == 3 && !model.cursor().has_selection(), "left moves the caret back");
        type_text(&mut model, "X");
        assert!(model.field().text == "helXlo" && model.cursor().pos == 4, "typing inserts at the caret");
        model.key(&key(KeyKind::Backspace, ""));
        assert!(model.field().text == "hello" && model.cursor().pos == 3, "backspace removes before the caret");
        model.key(&key(KeyKind::Delete, ""));
        assert!(model.field().text == "helo" && model.cursor().pos == 3, "delete removes after the caret");
        model.key(&key(KeyKind::Home, ""));
        assert_eq!(model.cursor().pos, 0, "home goes to the start");
        model.key(&key(KeyKind::Backspace, ""));
        assert_eq!(model.field().text, "helo", "backspace at the start does nothing");
        model.key(&key(KeyKind::End, ""));
        assert_eq!(model.cursor().pos, 4, "end goes to the end");
        model.key(&key(KeyKind::Delete, ""));
        assert_eq!(model.field().text, "helo", "delete at the end does nothing");
        model.key(&key(KeyKind::Right, ""));
        assert_eq!(model.cursor().pos, 4, "right stops at the end");

        model.key(&chord(KeyKind::Left, true, false));
        model.key(&chord(KeyKind::Left, true, false));
        assert!(selected(&model) == "lo" && model.cursor().pos == 2, "shift and left extend the selection");
        model.key(&chord(KeyKind::Right, true, false));
        assert_eq!(selected(&model), "o", "shift and right shrink the selection");
        model.key(&chord(KeyKind::Left, true, false));
        model.key(&key(KeyKind::Left, ""));
        assert!(!model.cursor().has_selection() && model.cursor().pos == 2, "left collapses a selection to its start");
        model.key(&chord(KeyKind::End, true, false));
        assert_eq!(selected(&model), "lo", "shift and end select to the end");
        model.key(&key(KeyKind::Right, ""));
        assert!(!model.cursor().has_selection() && model.cursor().pos == 4, "right collapses a selection to its end");
        model.key(&chord(KeyKind::Home, true, false));
        type_text(&mut model, "go");
        assert!(model.field().text == "go" && model.cursor().pos == 2, "typing replaces the selection");
        model.key(&key(KeyKind::Left, ""));
        model.key(&chord(KeyKind::Right, true, false));
        model.key(&key(KeyKind::Backspace, ""));
        assert_eq!(model.field().text, "g", "backspace deletes the selection");

        let mut words = Model::default();
        words.open(false);
        words.set_apps(Vec::new());
        type_text(&mut words, "foo bar");
        words.key(&chord(KeyKind::Left, false, true));
        assert_eq!(words.cursor().pos, 4, "ctrl and left jump to the word start");
        words.key(&chord(KeyKind::Left, false, true));
        assert_eq!(words.cursor().pos, 0, "ctrl and left jump again");
        words.key(&chord(KeyKind::Right, false, true));
        assert_eq!(words.cursor().pos, 3, "ctrl and right jump to the word end");
        words.key(&chord(KeyKind::Right, true, true));
        assert!(selected(&words) == " bar" && words.cursor().pos == 7, "ctrl, shift and right select by word");

        let mut utf = Model::default();
        utf.open(false);
        utf.set_apps(Vec::new());
        type_text(&mut utf, "a");
        utf.key(&key(KeyKind::Text, "ñ"));
        type_text(&mut utf, "b");
        utf.key(&key(KeyKind::Left, ""));
        assert_eq!(utf.cursor().pos, 3, "the caret steps over a whole character");
        utf.key(&key(KeyKind::Left, ""));
        assert_eq!(utf.cursor().pos, 1, "the caret steps over a two byte character");
        utf.key(&key(KeyKind::Delete, ""));
        assert_eq!(utf.field().text, "ab", "delete removes a whole character");
        utf.key(&key(KeyKind::Backspace, ""));
        assert!(utf.field().text == "b" && utf.cursor().pos == 0, "backspace removes a whole character");
        assert_eq!(utf.query_anim().chars.len(), 1, "the typing animation follows edits in the middle");
    }

    #[test]
    fn launcher_clipboard_keys() {
        let _snap = Snap::on();
        let mut model = Model::default();
        model.open(false);
        model.set_search_root("/nonexistent-root");
        model.set_apps(Vec::new());
        let clipboard = Rc::new(FakeClipboard::default());
        model.set_clipboard(Some(clipboard.clone()));

        type_text(&mut model, "hello world");
        model.key(&shortcut('c'));
        assert!(clipboard.stored.borrow().is_empty() && model.field().text == "hello world", "copy without a selection does nothing and inserts no text");
        model.key(&shortcut('a'));
        assert_eq!(selected(&model), "hello world", "ctrl and a select everything");
        model.key(&shortcut('c'));
        assert!(*clipboard.stored.borrow() == "hello world" && model.field().text == "hello world", "ctrl and c copy the selection");

        model.key(&key(KeyKind::Right, ""));
        model.key(&shortcut('v'));
        assert!(model.field().text == "hello worldhello world" && model.cursor().pos == 22, "ctrl and v paste at the caret");

        model.key(&chord(KeyKind::Home, true, false));
        model.key(&shortcut('x'));
        assert!(model.field().text.is_empty() && *clipboard.stored.borrow() == "hello worldhello world", "ctrl and x cut the selection");
        *clipboard.stored.borrow_mut() = "a\nb\tc".into();
        model.key(&shortcut('v'));
        assert_eq!(model.field().text, "a b c", "pasted line breaks become spaces");
        model.key(&key(KeyKind::Left, ""));
        clipboard.stored.borrow_mut().clear();
        model.key(&shortcut('v'));
        assert_eq!(model.field().text, "a b c", "pasting nothing changes nothing");

        let chord_text = KeyEvent { ctrl: true, base_sym: 'q' as u32, ..key(KeyKind::Text, "q") };
        model.key(&chord_text);
        let alt_text = KeyEvent { alt: true, ..key(KeyKind::Text, "q") };
        model.key(&alt_text);
        assert_eq!(model.field().text, "a b c", "ctrl and alt chords are not typed");

        clipboard.defer.set(true);
        model.key(&shortcut('v'));
        model.close();
        let late = clipboard.waiting.borrow_mut().take().unwrap();
        late("late".into());
        model.poll();
        assert!(model.field().text.is_empty(), "a paste that arrives after closing is dropped");

        let mut loose = Model::default();
        loose.open(false);
        loose.set_apps(Vec::new());
        type_text(&mut loose, "abc");
        loose.key(&shortcut('a'));
        loose.key(&shortcut('c'));
        loose.key(&shortcut('v'));
        assert_eq!(loose.field().text, "abc", "clipboard keys without a clipboard do nothing");
    }

    #[test]
    fn launcher_pointer_editing() {
        let _snap = Snap::on();
        let mut model = Model::default();
        model.open(false);
        model.set_search_root("/nonexistent-root");
        model.set_apps(Vec::new());
        type_text(&mut model, "hello world");

        let t0 = Instant::now();
        model.pointer_down(2.2, t0);
        assert!(model.cursor().pos == 2 && !model.cursor().has_selection(), "a click places the caret on the nearest boundary");
        model.pointer_drag(5.4);
        assert_eq!(selected(&model), "llo", "dragging selects");
        model.pointer_up();
        model.pointer_drag(9.0);
        assert_eq!(selected(&model), "llo", "moving without the button leaves the selection alone");
        model.pointer_down(40.0, t0 + Duration::from_secs(2));
        assert!(model.cursor().pos == 11 && !model.cursor().has_selection(), "a click past the text goes to the end");
        model.pointer_up();

        let t1 = t0 + Duration::from_secs(10);
        model.pointer_down(7.1, t1);
        model.pointer_up();
        model.pointer_down(7.1, t1 + Duration::from_millis(100));
        assert_eq!(selected(&model), "world", "a double click selects the word");
        model.pointer_drag(0.0);
        assert_eq!(selected(&model), "world", "a double click does not start a drag");
        model.pointer_up();

        model.pointer_down(7.1, t1 + Duration::from_secs(5));
        model.pointer_up();
        model.pointer_down(7.1, t1 + Duration::from_secs(5) + Duration::from_millis(900));
        assert!(!model.cursor().has_selection(), "slow clicks are not a double click");

        assert_eq!(model.field_window(20), 0, "short text needs no scrolling");
        model.key(&key(KeyKind::End, ""));
        assert_eq!(model.field_window(5), 6, "long text scrolls to keep the caret visible");
        model.key(&key(KeyKind::Home, ""));
        assert_eq!(model.field_window(5), 0, "moving the caret back scrolls back");
        model.key(&key(KeyKind::End, ""));
        assert_eq!(model.field_window(5), 6, "the end scrolls forward again");
        model.key(&key(KeyKind::Left, ""));
        assert_eq!(model.field_window(5), 6, "the window stays while the caret is inside it");
    }
}
