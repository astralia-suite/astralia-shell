//! Text field editing: a UTF-8 byte cursor with selection, key handling and the typing animations.

use crate::base::animation::{AnimationManager, Easing, Value, value, writer};
use crate::base::input::{KeyEvent, KeyKind};
use crate::render::geometry::Rect;

/// Removes the last character.
pub fn text_field_backspace(text: &mut String) {
    text.pop();
}

/// Bytes a password field reserves up front so typing never reallocates and leaves a stale copy behind.
pub const TEXT_FIELD_SECRET_CAPACITY: usize = 256;

/// A password buffer with `TEXT_FIELD_SECRET_CAPACITY` reserved.
pub fn text_field_secret() -> String {
    String::with_capacity(TEXT_FIELD_SECRET_CAPACITY)
}

/// Zeroes the whole allocation, including bytes left past the end by backspace, then empties the string.
pub fn text_field_wipe(text: &mut String) {
    let cap = text.capacity();
    if cap > 0 {
        // SAFETY: the pointer covers `cap` bytes owned by the string; the length is set to zero
        // right after, so the zeroed bytes never have to be valid UTF-8 content.
        unsafe { libc::explicit_bzero(text.as_mut_ptr().cast(), cap) };
    }
    text.clear();
}

/// Makes room for `extra` more bytes in a password buffer; a grow copies into a fresh buffer and wipes the old one.
pub fn text_field_secret_reserve(text: &mut String, extra: usize) {
    let needed = text.len() + extra;
    if needed <= text.capacity() {
        return;
    }
    let mut grown = String::with_capacity(needed.max(TEXT_FIELD_SECRET_CAPACITY).next_power_of_two());
    grown.push_str(text);
    text_field_wipe(text);
    *text = grown;
}

/// Byte positions into the text; `anchor` is the fixed end of the selection.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct TextCursor {
    pub pos: usize,
    pub anchor: usize,
}

impl TextCursor {
    pub fn has_selection(&self) -> bool {
        self.pos != self.anchor
    }

    pub fn begin(&self) -> usize {
        self.pos.min(self.anchor)
    }

    pub fn end(&self) -> usize {
        self.pos.max(self.anchor)
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum CursorDir {
    Left,
    Right,
}

fn is_continuation(text: &str, i: usize) -> bool {
    text.as_bytes()[i] & 0xC0 == 0x80
}

fn is_word_byte(c: u8) -> bool {
    c.is_ascii_alphanumeric() || c == b'_' || c >= 0x80
}

fn prev_boundary(text: &str, i: usize) -> usize {
    if i == 0 {
        return 0;
    }
    let mut i = i - 1;
    while i > 0 && is_continuation(text, i) {
        i -= 1;
    }
    i
}

fn next_boundary(text: &str, i: usize) -> usize {
    if i >= text.len() {
        return text.len();
    }
    let mut i = i + 1;
    while i < text.len() && is_continuation(text, i) {
        i += 1;
    }
    i
}

fn prev_word(text: &str, mut i: usize) -> usize {
    let b = text.as_bytes();
    while i > 0 && !is_word_byte(b[i - 1]) {
        i -= 1;
    }
    while i > 0 && is_word_byte(b[i - 1]) {
        i -= 1;
    }
    i
}

fn next_word(text: &str, mut i: usize) -> usize {
    let b = text.as_bytes();
    while i < b.len() && !is_word_byte(b[i]) {
        i += 1;
    }
    while i < b.len() && is_word_byte(b[i]) {
        i += 1;
    }
    i
}

fn move_to(c: &mut TextCursor, pos: usize, extend: bool) {
    c.pos = pos;
    if !extend {
        c.anchor = pos;
    }
}

/// Pulls both ends inside the text and back onto the start of the character they fall in.
pub fn text_cursor_clamp(text: &str, c: &mut TextCursor) {
    c.pos = text.floor_char_boundary(c.pos);
    c.anchor = text.floor_char_boundary(c.anchor);
}

/// One character left or right; without `extend` a selection collapses to its near end instead.
pub fn text_cursor_move(text: &str, c: &mut TextCursor, dir: CursorDir, extend: bool) {
    text_cursor_clamp(text, c);
    if !extend && c.has_selection() {
        let to = if dir == CursorDir::Left { c.begin() } else { c.end() };
        move_to(c, to, false);
        return;
    }
    let to = if dir == CursorDir::Left { prev_boundary(text, c.pos) } else { next_boundary(text, c.pos) };
    move_to(c, to, extend);
}

/// To the start of the previous word, or the end of the next one.
pub fn text_cursor_move_word(text: &str, c: &mut TextCursor, dir: CursorDir, extend: bool) {
    text_cursor_clamp(text, c);
    let to = if dir == CursorDir::Left { prev_word(text, c.pos) } else { next_word(text, c.pos) };
    move_to(c, to, extend);
}

pub fn text_cursor_home(c: &mut TextCursor, extend: bool) {
    move_to(c, 0, extend);
}

pub fn text_cursor_end(text: &str, c: &mut TextCursor, extend: bool) {
    move_to(c, text.len(), extend);
}

pub fn text_cursor_select_all(text: &str, c: &mut TextCursor) {
    c.anchor = 0;
    c.pos = text.len();
}

/// Selects the run of word bytes, or of separator bytes, around `byte`.
pub fn text_cursor_select_word(text: &str, c: &mut TextCursor, byte: usize) {
    if text.is_empty() {
        *c = TextCursor::default();
        return;
    }
    let b = text.as_bytes();
    let at = byte.min(b.len() - 1);
    let word = is_word_byte(b[at]);
    let mut from = at;
    while from > 0 && is_word_byte(b[from - 1]) == word {
        from -= 1;
    }
    let mut to = at + 1;
    while to < b.len() && is_word_byte(b[to]) == word {
        to += 1;
    }
    c.anchor = from;
    c.pos = to;
}

/// Places the cursor at `byte`, clamped and snapped back to a character start.
pub fn text_cursor_set(text: &str, c: &mut TextCursor, byte: usize, extend: bool) {
    move_to(c, text.floor_char_boundary(byte), extend);
}

pub fn text_cursor_delete_selection(text: &mut String, c: &mut TextCursor) -> bool {
    text_cursor_clamp(text, c);
    if !c.has_selection() {
        return false;
    }
    let begin = c.begin();
    text.replace_range(begin..c.end(), "");
    move_to(c, begin, false);
    true
}

/// Replaces the selection (if any) with `inserted`; true when the text changed.
pub fn text_cursor_insert(text: &mut String, c: &mut TextCursor, inserted: &str) -> bool {
    let removed = text_cursor_delete_selection(text, c);
    if inserted.is_empty() {
        return removed;
    }
    text.insert_str(c.pos, inserted);
    move_to(c, c.pos + inserted.len(), false);
    true
}

pub fn text_cursor_backspace(text: &mut String, c: &mut TextCursor) -> bool {
    if text_cursor_delete_selection(text, c) {
        return true;
    }
    if c.pos == 0 {
        return false;
    }
    let from = prev_boundary(text, c.pos);
    text.replace_range(from..c.pos, "");
    move_to(c, from, false);
    true
}

pub fn text_cursor_delete(text: &mut String, c: &mut TextCursor) -> bool {
    if text_cursor_delete_selection(text, c) {
        return true;
    }
    if c.pos >= text.len() {
        return false;
    }
    let to = next_boundary(text, c.pos);
    text.replace_range(c.pos..to, "");
    true
}

pub fn text_cursor_selected_text(text: &str, c: &TextCursor) -> String {
    text[text.floor_char_boundary(c.begin())..text.floor_char_boundary(c.end())].to_owned()
}

/// Byte offset of the `char_index`th character, or the text length past the end.
pub fn text_field_byte_offset(text: &str, char_index: usize) -> usize {
    text.char_indices().nth(char_index).map_or(text.len(), |(i, _)| i)
}

/// Number of characters that start before `byte`.
pub fn text_field_char_index(text: &str, byte: usize) -> usize {
    (0..byte.min(text.len())).filter(|&i| !is_continuation(text, i)).count()
}

pub fn text_field_utf8_len(text: &str) -> usize {
    text.chars().count()
}

#[derive(Clone, PartialEq)]
pub struct TextFieldState {
    pub text: String,
    /// Uncommitted input-method text, drawn after `text`.
    pub preedit: String,
    /// Blink phase of the caret while idle.
    pub cursor_idle_visible: bool,
    pub cursor_rect: Rect,
    pub error_message: String,
}

/// Prints lengths only: the field may hold a password.
impl std::fmt::Debug for TextFieldState {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("TextFieldState")
            .field("text_len", &self.text.len())
            .field("preedit_len", &self.preedit.len())
            .field("cursor_idle_visible", &self.cursor_idle_visible)
            .field("cursor_rect", &self.cursor_rect)
            .field("error_message", &self.error_message)
            .finish()
    }
}

impl Default for TextFieldState {
    fn default() -> Self {
        TextFieldState {
            text: String::new(),
            preedit: String::new(),
            cursor_idle_visible: true,
            cursor_rect: Rect::default(),
            error_message: String::new(),
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum TextFieldResult {
    None,
    Changed,
    Committed,
    Cancelled,
}

/// Append-only editing: text and preedit, backspace, enter and escape. Other keys are ignored.
pub fn text_field_handle_key(f: &mut TextFieldState, e: &KeyEvent) -> TextFieldResult {
    match e.kind {
        KeyKind::Text => {
            f.text.push_str(&e.text);
            f.preedit.clear();
        }
        KeyKind::Preedit => f.preedit.clone_from(&e.text),
        KeyKind::Backspace => {
            text_field_backspace(&mut f.text);
            f.preedit.clear();
        }
        KeyKind::Enter => {
            f.preedit.clear();
            return TextFieldResult::Committed;
        }
        KeyKind::Escape => {
            f.preedit.clear();
            return TextFieldResult::Cancelled;
        }
        _ => return TextFieldResult::None,
    }
    f.cursor_idle_visible = true;
    f.error_message.clear();
    TextFieldResult::Changed
}

/// Flips the caret blink phase; true means redraw.
pub fn text_field_idle_toggle(f: &mut TextFieldState) -> bool {
    f.cursor_idle_visible = !f.cursor_idle_visible;
    true
}

pub const TEXT_FIELD_POP_SCALE_MS: f32 = 200.0;
pub const TEXT_FIELD_POP_SLIDE_MS: f32 = 80.0;
pub const TEXT_FIELD_POP_SLIDE_OFFSET_PX: f32 = 8.0;
pub const TEXT_FIELD_ROW_SLIDE_MS: f32 = 200.0;
/// Owner slots per field; characters past this share owners with earlier ones.
pub const TEXT_FIELD_TYPE_ANIM_MAX: usize = 256;

/// One typed character popping in: scale from 0 and slide from the right.
pub struct TextFieldCharAnim {
    pub scale: Value,
    pub slide_x: Value,
}

#[derive(Default)]
pub struct TextFieldTypeAnim {
    pub chars: Vec<TextFieldCharAnim>,
}

/// The row's horizontal position, tweened toward each new anchor.
#[derive(Default)]
pub struct TextFieldRowSlide {
    pub x: Value,
    pub target: f32,
    pub primed: bool,
}

fn type_anim_owner(base: u64, index: usize, prop: u64) -> u64 {
    base + (index % TEXT_FIELD_TYPE_ANIM_MAX) as u64 * 2 + prop
}

fn type_anim_push(a: &mut TextFieldTypeAnim, am: &mut AnimationManager, owner_base: u64) {
    let idx = a.chars.len();
    let ch = TextFieldCharAnim { scale: value(0.0), slide_x: value(TEXT_FIELD_POP_SLIDE_OFFSET_PX) };
    am.animate(0.0, 1.0, TEXT_FIELD_POP_SCALE_MS, Easing::EaseOutBack, writer(&ch.scale), None, type_anim_owner(owner_base, idx, 0));
    am.animate(
        TEXT_FIELD_POP_SLIDE_OFFSET_PX,
        0.0,
        TEXT_FIELD_POP_SLIDE_MS,
        Easing::Linear,
        writer(&ch.slide_x),
        None,
        type_anim_owner(owner_base, idx, 1),
    );
    a.chars.push(ch);
}

fn type_anim_pop(a: &mut TextFieldTypeAnim, am: &mut AnimationManager, owner_base: u64) {
    if a.chars.pop().is_some() {
        let idx = a.chars.len();
        am.cancel_owner(type_anim_owner(owner_base, idx, 0));
        am.cancel_owner(type_anim_owner(owner_base, idx, 1));
    }
}

/// Matches the animation list to the character count: new characters pop in, removed ones drop.
pub fn text_field_type_anim_sync(a: &mut TextFieldTypeAnim, am: &mut AnimationManager, owner_base: u64, text: &str) {
    let target = text_field_utf8_len(text);
    while a.chars.len() > target {
        type_anim_pop(a, am, owner_base);
    }
    while a.chars.len() < target {
        type_anim_push(a, am, owner_base);
    }
}

/// Like `sync`, but every character is shown at rest with no animation.
pub fn text_field_type_anim_settle(a: &mut TextFieldTypeAnim, am: &mut AnimationManager, owner_base: u64, text: &str) {
    text_field_type_anim_clear(a, am, owner_base);
    for _ in 0..text_field_utf8_len(text) {
        a.chars.push(TextFieldCharAnim { scale: value(1.0), slide_x: value(0.0) });
    }
}

pub fn text_field_type_anim_clear(a: &mut TextFieldTypeAnim, am: &mut AnimationManager, owner_base: u64) {
    while !a.chars.is_empty() {
        type_anim_pop(a, am, owner_base);
    }
}

/// The row's x for this frame: the first anchor is taken as is, later changes tween to it.
pub fn text_field_row_slide(s: &mut TextFieldRowSlide, am: &mut AnimationManager, owner: u64, anchor_x: f32) -> f32 {
    if !s.primed {
        s.primed = true;
        s.target = anchor_x;
        s.x.set(anchor_x);
        return anchor_x;
    }
    if anchor_x != s.target {
        s.target = anchor_x;
        am.cancel_owner(owner);
        am.animate(s.x.get(), anchor_x, TEXT_FIELD_ROW_SLIDE_MS, Easing::EaseOutCubic, writer(&s.x), None, owner);
    }
    s.x.get()
}

pub fn text_field_row_slide_reset(s: &mut TextFieldRowSlide, am: &mut AnimationManager, owner: u64) {
    am.cancel_owner(owner);
    s.primed = false;
    s.x.set(0.0);
    s.target = 0.0;
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::base::input::{KeyEvent, KeyKind};
    use std::time::{Duration, Instant};

    #[test]
    fn text_field_wipe_zeroes() {
        let mut text = text_field_secret();
        text.push_str("hunter2!");
        text_field_backspace(&mut text);
        let (ptr, cap) = (text.as_ptr(), text.capacity());
        text_field_wipe(&mut text);
        assert_eq!(text.len(), 0);
        assert_eq!(text.capacity(), cap, "capacity kept");
        // SAFETY: `text` still owns the `cap` bytes at `ptr`.
        let bytes = unsafe { std::slice::from_raw_parts(ptr, cap) };
        assert!(bytes.iter().all(|&b| b == 0), "every byte zero, including the one backspace left");
        let mut empty = String::new();
        text_field_wipe(&mut empty);
        assert!(empty.is_empty());
    }

    #[test]
    fn text_cursor_editing() {
        let text = "añb c";
        let mut cursor = TextCursor::default();
        assert!(!cursor.has_selection() && cursor.begin() == 0 && cursor.end() == 0);

        text_cursor_end(text, &mut cursor, false);
        assert!(cursor.pos == text.len() && cursor.anchor == text.len());
        for _ in 0..3 {
            text_cursor_move(text, &mut cursor, CursorDir::Left, false);
        }
        assert_eq!(cursor.pos, 3, "left steps over characters");
        text_cursor_move(text, &mut cursor, CursorDir::Left, false);
        assert_eq!(cursor.pos, 1, "left steps over a multibyte character");
        text_cursor_move(text, &mut cursor, CursorDir::Right, true);
        assert!(cursor.pos == 3 && cursor.anchor == 1 && cursor.has_selection());
        assert_eq!(text_cursor_selected_text(text, &cursor), "ñ");
        text_cursor_move(text, &mut cursor, CursorDir::Left, false);
        assert!(cursor.pos == 1 && !cursor.has_selection(), "collapses to the start");

        let mut stale = TextCursor { pos: 99, anchor: 99 };
        text_cursor_clamp(text, &mut stale);
        assert!(stale.pos == text.len() && stale.anchor == text.len());

        let mut inside = TextCursor::default();
        text_cursor_set(text, &mut inside, 2, false);
        assert_eq!(inside.pos, 1, "inside a character snaps back");
        text_cursor_set(text, &mut inside, 100, true);
        assert!(inside.pos == text.len() && inside.anchor == 1);

        let mut words = TextCursor::default();
        text_cursor_end(text, &mut words, false);
        text_cursor_move_word(text, &mut words, CursorDir::Left, false);
        assert_eq!(words.pos, text.len() - 1, "single letter word");
        text_cursor_move_word(text, &mut words, CursorDir::Left, false);
        assert_eq!(words.pos, 0, "multibyte letter stays in the word");
        text_cursor_move_word(text, &mut words, CursorDir::Right, true);
        assert!(words.pos == 4 && words.anchor == 0);

        let mut pick = TextCursor::default();
        text_cursor_select_word(text, &mut pick, 2);
        assert_eq!(text_cursor_selected_text(text, &pick), "añb");
        text_cursor_select_word(text, &mut pick, 5);
        assert_eq!(text_cursor_selected_text(text, &pick), "c");
        text_cursor_select_word(text, &mut pick, 4);
        assert_eq!(text_cursor_selected_text(text, &pick), " ");
        let mut none = TextCursor { pos: 3, anchor: 1 };
        text_cursor_select_word("", &mut none, 0);
        assert!(none.pos == 0 && none.anchor == 0);

        let mut edit = String::from("abcd");
        let mut at = TextCursor::default();
        text_cursor_set(&edit, &mut at, 2, false);
        assert!(text_cursor_insert(&mut edit, &mut at, "XY") && edit == "abXYcd" && at.pos == 4 && at.anchor == 4);
        assert!(!text_cursor_insert(&mut edit, &mut at, ""));
        text_cursor_set(&edit, &mut at, 1, false);
        text_cursor_set(&edit, &mut at, 5, true);
        assert!(text_cursor_insert(&mut edit, &mut at, "-") && edit == "a-d" && at.pos == 2);
        assert!(text_cursor_backspace(&mut edit, &mut at) && edit == "ad" && at.pos == 1);
        assert!(text_cursor_delete(&mut edit, &mut at) && edit == "a" && at.pos == 1);
        assert!(!text_cursor_delete(&mut edit, &mut at));
        text_cursor_home(&mut at, false);
        assert!(!text_cursor_backspace(&mut edit, &mut at));
        text_cursor_select_all(&edit, &mut at);
        assert!(text_cursor_delete_selection(&mut edit, &mut at) && edit.is_empty() && at.pos == 0);
        assert!(!text_cursor_delete_selection(&mut edit, &mut at));

        let wide = "añ😀b";
        assert!(
            text_field_char_index(wide, 0) == 0
                && text_field_char_index(wide, 1) == 1
                && text_field_char_index(wide, 3) == 2
                && text_field_char_index(wide, wide.len()) == 4
        );
        assert!(
            text_field_byte_offset(wide, 0) == 0
                && text_field_byte_offset(wide, 2) == 3
                && text_field_byte_offset(wide, 3) == 7
                && text_field_byte_offset(wide, 99) == wide.len()
        );
    }

    #[test]
    fn cursor_inside_a_character_never_splits_it() {
        let mut text = String::from("añb");
        let mut c = TextCursor { pos: 2, anchor: 2 }; // inside "ñ"
        assert!(text_cursor_insert(&mut text, &mut c, "x"));
        assert_eq!(text, "axñb");
        let mut c = TextCursor { pos: 2, anchor: 0 };
        assert_eq!(text_cursor_selected_text("añb", &c), "a");
        let mut text = String::from("añb");
        assert!(text_cursor_delete_selection(&mut text, &mut c));
        assert_eq!(text, "ñb");
    }

    #[test]
    fn handle_key_edits() {
        let mut f = TextFieldState { text: "añ".into(), error_message: "bad".into(), ..Default::default() };
        let key = |kind, text: &str| KeyEvent { kind, text: text.into(), ..Default::default() };
        assert_eq!(text_field_handle_key(&mut f, &key(KeyKind::Backspace, "")), TextFieldResult::Changed);
        assert_eq!((f.text.as_str(), f.error_message.as_str()), ("a", ""));
        assert_eq!(text_field_handle_key(&mut f, &key(KeyKind::Preedit, "k")), TextFieldResult::Changed);
        assert_eq!(f.preedit, "k");
        assert_eq!(text_field_handle_key(&mut f, &key(KeyKind::Text, "か")), TextFieldResult::Changed);
        assert_eq!((f.text.as_str(), f.preedit.as_str()), ("aか", ""));
        assert_eq!(text_field_handle_key(&mut f, &key(KeyKind::Enter, "")), TextFieldResult::Committed);
        assert_eq!(text_field_handle_key(&mut f, &key(KeyKind::Escape, "")), TextFieldResult::Cancelled);
        assert_eq!(text_field_handle_key(&mut f, &key(KeyKind::Tab, "")), TextFieldResult::None);
    }

    #[test]
    fn type_anim_tracks_length() {
        let mut am = AnimationManager::default();
        let mut a = TextFieldTypeAnim::default();
        let start = Instant::now();
        text_field_type_anim_sync(&mut a, &mut am, 100, "añ");
        assert_eq!(a.chars.len(), 2);
        assert_eq!((a.chars[1].scale.get(), a.chars[1].slide_x.get()), (0.0, TEXT_FIELD_POP_SLIDE_OFFSET_PX));
        am.tick(start + Duration::from_secs(1));
        assert_eq!((a.chars[1].scale.get(), a.chars[1].slide_x.get()), (1.0, 0.0));
        text_field_type_anim_sync(&mut a, &mut am, 100, "a");
        assert_eq!(a.chars.len(), 1);
        text_field_type_anim_settle(&mut a, &mut am, 100, "abc");
        assert_eq!(a.chars.len(), 3);
        assert!(!am.has_active());
    }

    #[test]
    fn row_slide_snaps_then_tweens() {
        let mut am = AnimationManager::default();
        let mut s = TextFieldRowSlide::default();
        assert_eq!(text_field_row_slide(&mut s, &mut am, 7, 40.0), 40.0); // first value snaps
        assert_eq!(text_field_row_slide(&mut s, &mut am, 7, 80.0), 40.0); // then tweens from there
        am.tick(Instant::now() + Duration::from_secs(1));
        assert_eq!(s.x.get(), 80.0);
        text_field_row_slide_reset(&mut s, &mut am, 7);
        assert!(!s.primed && s.x.get() == 0.0);
    }
}
