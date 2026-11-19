//! Paints a single-line input on a monospace cell grid: selection, text (popping in as typed),
//! preedit and caret.

use crate::render::canvas::{Canvas, GroupOptions, TextStyle};
use crate::render::geometry::Rect;
use crate::render::text_field::{TextFieldState, TextFieldTypeAnim, text_field_byte_offset, text_field_utf8_len};
use crate::render::theme::palette;
use crate::render::tokens::Color;

#[derive(Clone, Copy, Debug)]
pub struct FieldDraw {
    pub style: TextStyle,
    pub x: f32,
    pub center_y: f32,
    pub caret_height: f32,
    pub caret_width: f32,
    pub caret_gap: f32,
    pub color: Color,
    pub selection: Color,
    /// Index into the typing animation of the first displayed character.
    pub first_index: usize,
}

impl FieldDraw {
    pub fn new(style: TextStyle) -> FieldDraw {
        let p = palette();
        FieldDraw {
            style,
            x: 0.0,
            center_y: 0.0,
            caret_height: 0.0,
            caret_width: 2.0,
            caret_gap: 0.0,
            color: p.text,
            selection: p.accent_alt_alpha50,
            first_index: 0,
        }
    }
}

/// Caret column and selection, in characters of the displayed text.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct FieldCursor {
    pub column: usize,
    pub selection_from: usize,
    pub selection_to: usize,
}

fn settled(anim: &TextFieldTypeAnim) -> bool {
    anim.chars.iter().all(|c| c.scale.get() == 1.0 && c.slide_x.get() == 0.0)
}

/// Draws the field and returns the caret rectangle. Without a cursor the caret sits at the end
/// and the preedit is drawn there without moving it.
pub fn draw_field_input(
    canvas: &mut dyn Canvas,
    display: &str,
    field: &TextFieldState,
    anim: &TextFieldTypeAnim,
    draw: &FieldDraw,
    cursor: Option<&FieldCursor>,
) -> Rect {
    let style = &draw.style;
    let cell = canvas.advance(style);
    let length = text_field_utf8_len(display);
    let column = cursor.map_or(length, |c| c.column.min(length));
    let preedit_w = if field.preedit.is_empty() { 0.0 } else { canvas.measure(&field.preedit, style).w };
    if let Some(c) = cursor.filter(|c| c.selection_to > c.selection_from) {
        let from = c.selection_from.min(length);
        let to = c.selection_to.min(length);
        let shift = if preedit_w > 0.0 && from >= column { preedit_w } else { 0.0 };
        let r = Rect {
            x: draw.x + from as f32 * cell + shift,
            y: draw.center_y - draw.caret_height / 2.0,
            w: (to - from) as f32 * cell,
            h: draw.caret_height,
        };
        canvas.rect(r, draw.selection);
    }
    if !display.is_empty() {
        let text_at = |canvas: &mut dyn Canvas, s: &str, x: f32| {
            let size = canvas.measure(s, style);
            canvas.text(s, style, x, draw.center_y - size.h / 2.0, draw.color);
        };
        if settled(anim) && preedit_w <= 0.0 {
            text_at(canvas, display, draw.x);
        } else if settled(anim) {
            let (head, tail) = display.split_at(text_field_byte_offset(display, column));
            if !head.is_empty() {
                text_at(canvas, head, draw.x);
            }
            if !tail.is_empty() {
                text_at(canvas, tail, draw.x + column as f32 * cell + preedit_w);
            }
        } else {
            for (index, (at, ch)) in display.char_indices().enumerate() {
                let ch = &display[at..at + ch.len_utf8()];
                let a = anim.chars.get(draw.first_index + index);
                let scale = a.map_or(1.0, |a| a.scale.get());
                let slide = a.map_or(0.0, |a| a.slide_x.get());
                if scale <= 0.0 {
                    continue;
                }
                let size = canvas.measure(ch, style);
                let shift = if index >= column { preedit_w } else { 0.0 };
                let center = draw.x + index as f32 * cell + cell / 2.0 + slide + shift;
                let r = Rect { x: center - size.w / 2.0, y: draw.center_y - size.h / 2.0, w: size.w, h: size.h };
                canvas.begin_group(r, GroupOptions { scale, ..GroupOptions::default() });
                canvas.text(ch, style, 0.0, 0.0, draw.color);
                canvas.end_group();
            }
        }
    }
    let mut caret_x = draw.x + cell * column as f32 + draw.caret_gap;
    if !field.preedit.is_empty() {
        let size = canvas.measure(&field.preedit, style);
        let y = draw.center_y - size.h / 2.0;
        canvas.text(&field.preedit, style, caret_x, y, draw.color);
        canvas.rect(Rect { x: caret_x, y: y + size.h, w: size.w, h: 1.0 }, draw.color);
        if cursor.is_some() {
            caret_x += size.w;
        }
    }
    let caret = Rect { x: caret_x, y: draw.center_y - draw.caret_height / 2.0, w: draw.caret_width, h: draw.caret_height };
    if field.cursor_idle_visible {
        canvas.rect(caret, draw.color);
    }
    caret
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::base::animation::AnimationManager;
    use crate::render::recording_canvas::{OpKind, RecordingCanvas};
    use crate::render::text_field::{text_field_type_anim_settle, text_field_type_anim_sync};

    #[test]
    fn field_settled_draws_text_and_caret() {
        let mut c = RecordingCanvas::default();
        let mut am = AnimationManager::default();
        let mut anim = TextFieldTypeAnim::default();
        text_field_type_anim_settle(&mut anim, &mut am, 1, "abc");
        let draw = FieldDraw { x: 10.0, center_y: 50.0, caret_height: 20.0, ..FieldDraw::new(TextStyle::default()) };
        let caret = draw_field_input(&mut c, "abc", &TextFieldState::default(), &anim, &draw, None);
        assert_eq!(caret, Rect { x: 37.0, y: 40.0, w: 2.0, h: 20.0 });
        assert_eq!((c.count(OpKind::Text), c.ops[0].text.as_str(), c.ops[0].rect.y), (1, "abc", 40.0));
        assert_eq!(c.ops.last().unwrap().rect, caret);
    }

    #[test]
    fn field_popping_chars_draw_in_groups() {
        let mut c = RecordingCanvas::default();
        let mut am = AnimationManager::default();
        let mut anim = TextFieldTypeAnim::default();
        text_field_type_anim_sync(&mut anim, &mut am, 1, "ab");
        anim.chars[0].scale.set(0.5);
        let draw = FieldDraw::new(TextStyle::default());
        draw_field_input(&mut c, "ab", &TextFieldState::default(), &anim, &draw, None);
        assert_eq!(c.count(OpKind::BeginGroup), 1); // char 1 still has scale 0 and is skipped
        assert_eq!(c.ops[0].value, 0.5);
    }

    #[test]
    fn field_selection_rect() {
        let mut c = RecordingCanvas::default();
        let draw = FieldDraw { x: 10.0, center_y: 50.0, caret_height: 20.0, ..FieldDraw::new(TextStyle::default()) };
        let cursor = FieldCursor { column: 1, selection_from: 0, selection_to: 2 };
        draw_field_input(&mut c, "abc", &TextFieldState::default(), &TextFieldTypeAnim::default(), &draw, Some(&cursor));
        assert_eq!((c.ops[0].kind, c.ops[0].rect), (OpKind::Rect, Rect { x: 10.0, y: 40.0, w: 18.0, h: 20.0 }));
        assert_eq!(c.ops[0].color, draw.selection);
    }

    #[test]
    fn field_preedit_splits_text_at_the_caret() {
        let mut c = RecordingCanvas::default();
        let field = TextFieldState { preedit: "k".into(), ..Default::default() };
        let draw = FieldDraw { x: 10.0, center_y: 50.0, caret_height: 20.0, ..FieldDraw::new(TextStyle::default()) };
        let cursor = FieldCursor { column: 1, selection_from: 0, selection_to: 0 };
        let caret = draw_field_input(&mut c, "añ", &field, &TextFieldTypeAnim::default(), &draw, Some(&cursor));
        let texts: Vec<(&str, f32)> = c.ops.iter().filter(|o| o.kind == OpKind::Text).map(|o| (o.text.as_str(), o.rect.x)).collect();
        assert_eq!(texts, [("a", 10.0), ("ñ", 28.0), ("k", 19.0)]); // the tail moves right by the preedit width
        assert_eq!(caret.x, 28.0);
    }
}
