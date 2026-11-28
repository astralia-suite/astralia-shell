//! A single-line input with a caret, selection, clipboard keys, pointer selection and a scroll
//! window; the launcher field and the settings fields share it.

use std::cell::RefCell;
use std::rc::{Rc, Weak};
use std::time::{Duration, Instant};

use crate::base::animation::AnimationManager;
use crate::base::input::{Clipboard, KeyEvent, KeyKind};
use crate::render::canvas::Canvas;
use crate::render::field_view::{FieldCursor, FieldDraw, draw_field_input};
use crate::render::geometry::Rect;
use crate::render::text_field::{
    CursorDir, TextCursor, TextFieldState, TextFieldTypeAnim, text_cursor_backspace, text_cursor_clamp, text_cursor_delete,
    text_cursor_delete_selection, text_cursor_end, text_cursor_home, text_cursor_insert, text_cursor_move, text_cursor_move_word,
    text_cursor_select_all, text_cursor_select_word, text_cursor_selected_text, text_cursor_set, text_field_byte_offset,
    text_field_char_index, text_field_idle_toggle, text_field_type_anim_clear, text_field_type_anim_settle, text_field_type_anim_sync,
    text_field_utf8_len,
};

/// Two presses on the same spot within this are a double click.
pub const DOUBLE_CLICK_MS: u64 = 400;

/// What a key did; the keys the input does not edit with are handed back.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum InputKey {
    /// The text changed.
    Edited,
    /// The caret, selection or preedit changed, or a paste was asked for.
    Moved,
    Enter,
    Escape,
    Up,
    Down,
    Tab,
    Ignored,
}

/// Where a drawn input sits, for pointer hits and the input method.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct InputFrame {
    pub caret: Rect,
    pub text_x: f32,
    pub cell_w: f32,
    pub first: usize,
}

impl InputFrame {
    /// The character column under `x`, from the start of the text.
    pub fn column(&self, x: f32) -> f32 {
        if self.cell_w <= 0.0 {
            return 0.0;
        }
        (self.first as f32 + (x - self.text_x) / self.cell_w).max(0.0)
    }

    pub fn shifted(self, dx: f32, dy: f32) -> InputFrame {
        InputFrame { caret: Rect { x: self.caret.x + dx, y: self.caret.y + dy, ..self.caret }, text_x: self.text_x + dx, ..self }
    }
}

pub struct TextInput {
    field: TextFieldState,
    cursor: TextCursor,
    anim: TextFieldTypeAnim,
    anim_owner: u64,
    first: usize,
    dragging: bool,
    last_click: Option<(Instant, usize)>,
    clipboard: Option<Rc<dyn Clipboard>>,
    /// Pastes landed since the last poll; swapped out on clear so a late one is dropped.
    pastes: Rc<RefCell<Vec<String>>>,
    wake: Option<Rc<dyn Fn()>>,
}

impl TextInput {
    /// `anim_owner` is the first of the animation owners the typing animation uses.
    pub fn new(anim_owner: u64) -> TextInput {
        TextInput {
            field: TextFieldState::default(),
            cursor: TextCursor::default(),
            anim: TextFieldTypeAnim::default(),
            anim_owner,
            first: 0,
            dragging: false,
            last_click: None,
            clipboard: None,
            pastes: Rc::default(),
            wake: None,
        }
    }

    pub fn set_clipboard(&mut self, clipboard: Option<Rc<dyn Clipboard>>) {
        self.clipboard = clipboard;
    }

    /// Called when a paste lands, so the host polls.
    pub fn set_wake(&mut self, wake: Rc<dyn Fn()>) {
        self.wake = Some(wake);
    }

    pub fn text(&self) -> &str {
        &self.field.text
    }

    #[cfg(test)]
    pub fn field(&self) -> &TextFieldState {
        &self.field
    }

    #[cfg(test)]
    pub fn cursor(&self) -> &TextCursor {
        &self.cursor
    }

    #[cfg(test)]
    pub fn anim(&self) -> &TextFieldTypeAnim {
        &self.anim
    }

    pub fn dragging(&self) -> bool {
        self.dragging
    }

    /// Replaces the text, caret at the end, without the typing animation.
    pub fn set_text(&mut self, am: &mut AnimationManager, text: String) {
        self.cursor = TextCursor { pos: text.len(), anchor: text.len() };
        self.field.text = text;
        self.field.preedit.clear();
        self.field.cursor_idle_visible = true;
        (self.first, self.dragging, self.last_click) = (0, false, None);
        text_field_type_anim_settle(&mut self.anim, am, self.anim_owner, &self.field.text);
    }

    /// Empties the input; a paste still on its way is dropped.
    pub fn clear(&mut self, am: &mut AnimationManager) {
        self.field.text.clear();
        self.field.preedit.clear();
        self.field.cursor_idle_visible = true;
        self.cursor = TextCursor::default();
        (self.first, self.dragging, self.last_click) = (0, false, None);
        self.pastes = Rc::default();
        text_field_type_anim_clear(&mut self.anim, am, self.anim_owner);
    }

    pub fn clear_preedit(&mut self) {
        self.field.preedit.clear();
    }

    pub fn toggle_caret(&mut self) -> bool {
        text_field_idle_toggle(&mut self.field)
    }

    /// After every text change: the preedit goes, the caret shows, and characters typed at the end pop in.
    fn edited(&mut self, am: &mut AnimationManager) {
        self.field.preedit.clear();
        self.field.cursor_idle_visible = true;
        text_cursor_clamp(&self.field.text, &mut self.cursor);
        if self.cursor.pos == self.field.text.len() {
            text_field_type_anim_sync(&mut self.anim, am, self.anim_owner, &self.field.text);
        } else {
            text_field_type_anim_settle(&mut self.anim, am, self.anim_owner, &self.field.text);
        }
    }

    fn moved(&mut self) -> InputKey {
        self.field.cursor_idle_visible = true;
        InputKey::Moved
    }

    pub fn key(&mut self, am: &mut AnimationManager, event: &KeyEvent) -> InputKey {
        let (text, cursor) = (&mut self.field.text, &mut self.cursor);
        let edited = match event.kind {
            KeyKind::Text => return self.key_text(am, event),
            KeyKind::Preedit => {
                self.field.preedit.clone_from(&event.text);
                return self.moved();
            }
            // Backspace counts as an edit even at the start, so the launcher leaves a submenu.
            KeyKind::Backspace => {
                text_cursor_backspace(text, cursor);
                true
            }
            KeyKind::Delete => text_cursor_delete(text, cursor),
            KeyKind::Left | KeyKind::Right => {
                let dir = if event.kind == KeyKind::Left { CursorDir::Left } else { CursorDir::Right };
                if event.ctrl {
                    text_cursor_move_word(text, cursor, dir, event.shift);
                } else {
                    text_cursor_move(text, cursor, dir, event.shift);
                }
                return self.moved();
            }
            KeyKind::Home => {
                text_cursor_home(cursor, event.shift);
                return self.moved();
            }
            KeyKind::End => {
                text_cursor_end(text, cursor, event.shift);
                return self.moved();
            }
            KeyKind::Up => return InputKey::Up,
            KeyKind::Down => return InputKey::Down,
            KeyKind::Escape => return InputKey::Escape,
            KeyKind::Enter => return InputKey::Enter,
            KeyKind::Tab => return InputKey::Tab,
        };
        if !edited {
            return InputKey::Moved;
        }
        self.edited(am);
        InputKey::Edited
    }

    fn key_text(&mut self, am: &mut AnimationManager, event: &KeyEvent) -> InputKey {
        if event.alt {
            return InputKey::Ignored;
        }
        if !event.ctrl {
            if event.text.is_empty() {
                return InputKey::Ignored;
            }
            text_cursor_insert(&mut self.field.text, &mut self.cursor, &event.text);
            self.edited(am);
            return InputKey::Edited;
        }
        match char::from_u32(event.base_sym).map(|c| c.to_ascii_lowercase()) {
            Some('a') => {
                text_cursor_select_all(&self.field.text, &mut self.cursor);
                self.moved()
            }
            Some(sym @ ('c' | 'x')) => {
                if self.cursor.has_selection()
                    && let Some(clipboard) = &self.clipboard
                {
                    clipboard.set(text_cursor_selected_text(&self.field.text, &self.cursor));
                }
                if sym == 'x' && text_cursor_delete_selection(&mut self.field.text, &mut self.cursor) {
                    self.edited(am);
                    return InputKey::Edited;
                }
                InputKey::Moved
            }
            Some('v') => {
                if let Some(clipboard) = &self.clipboard {
                    let (pastes, wake): (Weak<RefCell<Vec<String>>>, _) = (Rc::downgrade(&self.pastes), self.wake.clone());
                    clipboard.request(Box::new(move |text| {
                        if let Some(pastes) = pastes.upgrade() {
                            pastes.borrow_mut().push(text);
                            if let Some(wake) = wake {
                                wake();
                            }
                        }
                    }));
                }
                InputKey::Moved
            }
            _ => InputKey::Ignored,
        }
    }

    /// Applies the pastes landed since the last call, as one line; true when the text changed.
    pub fn poll(&mut self, am: &mut AnimationManager) -> bool {
        let pastes = std::mem::take(&mut *self.pastes.borrow_mut());
        let mut changed = false;
        for text in pastes {
            let line: String = text
                .chars()
                .map(|c| if matches!(c, '\n' | '\r' | '\t') { ' ' } else { c })
                .filter(|&c| c >= ' ' && c != '\u{7f}')
                .collect();
            if !line.is_empty() {
                text_cursor_insert(&mut self.field.text, &mut self.cursor, &line);
                changed = true;
            }
        }
        if changed {
            self.edited(am);
        }
        changed
    }

    /// Input-method text, inserted at the caret.
    pub fn commit_text(&mut self, am: &mut AnimationManager, text: &str) {
        text_cursor_insert(&mut self.field.text, &mut self.cursor, text);
        self.edited(am);
    }

    /// Input-method deletion of `count` characters before the caret.
    pub fn delete_before(&mut self, am: &mut AnimationManager, count: u32) {
        text_cursor_clamp(&self.field.text, &mut self.cursor);
        self.cursor.anchor = self.cursor.pos;
        for _ in 0..count {
            text_cursor_backspace(&mut self.field.text, &mut self.cursor);
        }
        self.edited(am);
    }

    pub fn set_preedit(&mut self, text: &str) {
        self.field.preedit = text.to_string();
        self.field.cursor_idle_visible = true;
    }

    fn byte_at_column(&self, column: f32) -> usize {
        text_field_byte_offset(&self.field.text, (column.max(0.0) + 0.5) as usize)
    }

    /// A press at a character `column`; a second press on the same spot soon after selects the word.
    pub fn pointer_down(&mut self, column: f32, now: Instant) {
        let byte = self.byte_at_column(column);
        let double_click = self
            .last_click
            .is_some_and(|(at, at_byte)| at_byte == byte && now.saturating_duration_since(at) <= Duration::from_millis(DOUBLE_CLICK_MS));
        self.field.cursor_idle_visible = true;
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
    }

    /// Extends the selection while the button is held; true when it did.
    pub fn pointer_drag(&mut self, column: f32) -> bool {
        if !self.dragging {
            return false;
        }
        let byte = self.byte_at_column(column);
        text_cursor_set(&self.field.text, &mut self.cursor, byte, true);
        self.field.cursor_idle_visible = true;
        true
    }

    pub fn pointer_up(&mut self) {
        self.dragging = false;
    }

    /// The first character shown in a field `width` characters wide, scrolled to keep the caret in view.
    pub fn window(&mut self, width: usize) -> usize {
        let total = text_field_utf8_len(&self.field.text);
        if total <= width {
            self.first = 0;
            return 0;
        }
        let column = text_field_char_index(&self.field.text, self.cursor.pos);
        if column < self.first {
            self.first = column;
        } else if column > self.first + width {
            self.first = column - width;
        }
        self.first = self.first.min(total - width);
        self.first
    }

    /// Draws the part of the text that fits `width` characters, with selection and caret.
    pub fn draw(&mut self, canvas: &mut dyn Canvas, draw: FieldDraw, width: usize) -> InputFrame {
        let first = self.window(width);
        let text = &self.field.text;
        let last = text_field_utf8_len(text).min(first + width);
        let display = &text[text_field_byte_offset(text, first)..text_field_byte_offset(text, last)];
        let column_of = |byte: usize| text_field_char_index(text, byte).clamp(first, last) - first;
        let c = &self.cursor;
        let cursor = FieldCursor { column: column_of(c.pos), selection_from: column_of(c.begin()), selection_to: column_of(c.end()) };
        let draw = FieldDraw { first_index: first, ..draw };
        let caret = draw_field_input(canvas, display, &self.field, &self.anim, &draw, Some(&cursor));
        InputFrame { caret, text_x: draw.x, cell_w: canvas.advance(&draw.style), first }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::render::canvas::TextStyle;
    use crate::render::recording_canvas::{OpKind, RecordingCanvas};

    struct Board(RefCell<String>);

    impl Clipboard for Board {
        fn set(&self, text: String) {
            *self.0.borrow_mut() = text;
        }
        fn request(&self, done: Box<dyn FnOnce(String)>) {
            done(self.0.borrow().clone());
        }
    }

    fn press(input: &mut TextInput, am: &mut AnimationManager, kind: KeyKind, ctrl: bool, letter: char) -> InputKey {
        let text = if ctrl { String::new() } else { letter.to_string() };
        input.key(am, &KeyEvent { kind, ctrl, base_sym: letter as u32, text, ..KeyEvent::default() })
    }

    #[test]
    fn text_input_edits_pastes_and_draws() {
        let mut am = AnimationManager::default();
        let mut input = TextInput::new(1);
        let board = Rc::new(Board(RefCell::new("x\ny".into())));
        input.set_clipboard(Some(board.clone()));
        input.set_text(&mut am, "/home/me".into());
        assert_eq!(press(&mut input, &mut am, KeyKind::Text, true, 'a'), InputKey::Moved);
        assert_eq!(press(&mut input, &mut am, KeyKind::Text, false, 'q'), InputKey::Edited);
        assert_eq!(input.text(), "q", "typing replaces the selection");
        press(&mut input, &mut am, KeyKind::Text, true, 'v');
        assert!(input.poll(&mut am) && input.text() == "qx y", "a paste lands at the caret as one line");
        assert_eq!(press(&mut input, &mut am, KeyKind::Up, false, ' '), InputKey::Up, "keys it does not edit with are handed back");

        press(&mut input, &mut am, KeyKind::Text, true, 'v');
        input.clear(&mut am);
        assert!(!input.poll(&mut am) && input.text().is_empty(), "a paste queued before a clear is dropped");

        input.set_text(&mut am, "abcdef".into());
        let mut canvas = RecordingCanvas::default();
        let frame = input.draw(&mut canvas, FieldDraw { x: 10.0, ..FieldDraw::new(TextStyle::default()) }, 3);
        assert_eq!(frame.first, 3, "a long text scrolls to the caret");
        assert!(canvas.ops.iter().any(|o| o.kind == OpKind::Text && o.text == "def"), "only the window is drawn");
        input.pointer_down(frame.column(10.0), Instant::now());
        assert_eq!(input.cursor().pos, 3, "a click maps through the window");
    }
}
