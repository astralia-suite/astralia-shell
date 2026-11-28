//! Paints the launcher card: the mode chip, the field, the rows and the constellation bullets.

use crate::config::launcher_config as cfg;
use crate::render::canvas::{Canvas, FontFamily, GroupOptions, NO_IMAGE, TextStyle};
use crate::render::field_view::FieldDraw;
use crate::render::geometry::Rect;
use crate::render::icons;
use crate::render::sticker::{StickerStyle, selectable, sticker};
use crate::render::text_input::InputFrame;
use crate::render::theme::palette;
use crate::render::tokens::{WHITE, metrics, with_alpha};

use super::entrance::{EntranceFrame, EntranceState, entrance_sample};
use super::files::{elide, elide_middle};
use super::model::Model;
use super::search::LauncherMode;

const LABEL_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::LABEL_PX, bold: false, max_width: 0, wrap: false };
const SMALL_STYLE: TextStyle = TextStyle { family: FontFamily::Text, px: cfg::SMALL_PX, bold: false, max_width: 0, wrap: false };
const ICON_STYLE: TextStyle = TextStyle { family: FontFamily::Icon, px: cfg::ICON_SIZE, bold: false, max_width: 0, wrap: false };

/// What a paint laid out, for hit tests, the input method and the entrance lightning.
#[derive(Clone, Debug, Default)]
pub struct Frame {
    pub rect: Rect,
    pub caret: Rect,
    /// Fully visible rows and their index.
    pub rows: Vec<(Rect, usize)>,
    pub entrance: EntranceFrame,
    pub field: Rect,
    pub input: InputFrame,
}

fn mode_icon(mode: LauncherMode) -> &'static str {
    match mode {
        LauncherMode::Run => icons::TERMINAL,
        LauncherMode::Google => icons::BRAND_GOOGLE,
        LauncherMode::Youtube => icons::BRAND_YOUTUBE,
        LauncherMode::Duckduckgo | LauncherMode::Url => icons::LINK,
        LauncherMode::Drun => icons::APPS,
    }
}

pub fn field_hit(frame: &Frame, x: f32, y: f32) -> bool {
    let f = frame.field;
    f.w > 0.0 && x >= f.x && x < f.x + f.w && y >= f.y && y < f.y + f.h
}

/// The character column under `x` in the field, from its first shown character.
pub fn field_column(frame: &Frame, x: f32) -> f32 {
    frame.input.column(x)
}

pub fn paint(canvas: &mut dyn Canvas, model: &mut Model, width: f32, height: f32, entrance: Option<&EntranceState>) -> Frame {
    let p = palette();
    let mut frame = Frame::default();
    let inset = cfg::CLIP_INSET;
    let box_h = model.box_height();
    let box_x = (width - cfg::WIDTH) / 2.0;
    let box_y = (height - box_h) / 2.0;
    if let Some(entrance) = entrance {
        frame.entrance = entrance_sample(entrance, Rect { x: box_x, y: box_y, w: cfg::WIDTH, h: box_h });
        canvas.set_opacity(1.0);
        if frame.entrance.scrim > 0.0 {
            canvas.rect(Rect { x: 0.0, y: 0.0, w: width, h: height }, with_alpha(p.shadow, frame.entrance.scrim));
        }
        canvas.set_opacity(frame.entrance.opacity);
    }
    frame.rect = Rect { x: box_x, y: box_y, w: cfg::WIDTH, h: box_h };
    let card = StickerStyle { outline: p.border_outer, border_width: cfg::MENU_BORDER_WIDTH, ..StickerStyle::default() };
    sticker(canvas, frame.rect, metrics::RADIUS_CARD, p.paper, &card);

    let (ox, oy) = (box_x + inset, box_y + inset);
    let clip = GroupOptions { clip: true, ..GroupOptions::default() };
    canvas.begin_group(Rect { x: ox, y: oy, w: cfg::WIDTH - 2.0 * inset, h: box_h - 2.0 * inset }, clip);

    let chip = StickerStyle { outline: p.border_outer, border_width: cfg::BORDER_WIDTH, shadow: 0.0, ..StickerStyle::default() };
    let mode_x = box_x + cfg::MENU_PAD;
    let field_top = box_y + cfg::MENU_PAD;
    sticker(canvas, Rect { x: mode_x - ox, y: field_top - oy, w: cfg::SEARCH_HEIGHT, h: cfg::SEARCH_HEIGHT }, metrics::RADIUS_WIDGET, p.surface, &chip);
    let mode_glyph = mode_icon(model.mode());
    let mode_size = canvas.measure(mode_glyph, &ICON_STYLE);
    canvas.text(
        mode_glyph,
        &ICON_STYLE,
        mode_x - ox + (cfg::SEARCH_HEIGHT - mode_size.w) / 2.0,
        field_top - oy + (cfg::SEARCH_HEIGHT - mode_size.h) / 2.0,
        p.text,
    );

    let field_x = mode_x + cfg::SEARCH_HEIGHT + cfg::PAD;
    let field_w = box_x + cfg::WIDTH - cfg::MENU_PAD - field_x;
    sticker(canvas, Rect { x: field_x - ox, y: field_top - oy, w: field_w, h: cfg::SEARCH_HEIGHT }, metrics::RADIUS_WIDGET, p.surface, &chip);
    let text_x = field_x + cfg::PAD;
    let center_y = field_top + cfg::SEARCH_HEIGHT / 2.0;

    let draw = FieldDraw {
        x: text_x - ox,
        center_y: center_y - oy,
        caret_height: cfg::SEARCH_HEIGHT - 2.0 * cfg::PAD,
        caret_width: cfg::CARET_WIDTH,
        ..FieldDraw::new(LABEL_STYLE)
    };
    frame.input = model.input_mut().draw(canvas, draw, cfg::MAX_ROW_CHARS).shifted(ox, oy);
    frame.caret = frame.input.caret;
    frame.field = Rect { x: field_x, y: field_top, w: field_w, h: cfg::SEARCH_HEIGHT };

    let content_x = mode_x + cfg::SEARCH_HEIGHT + cfg::BULLET_GAP;
    let list_top = box_y + cfg::LIST_TOP;
    let list_h = box_y + box_h - inset - list_top;
    canvas.begin_group(Rect { x: mode_x - ox, y: list_top - oy, w: cfg::WIDTH - 2.0 * cfg::MENU_PAD, h: list_h }, clip);

    let row_x = content_x - mode_x;
    let row_w = box_x + cfg::WIDTH - cfg::MENU_PAD - content_x;
    let rows = model.rows();
    let (selected, hovered, scroll) = (model.selected(), model.hovered(), model.scroll_offset());
    let row_y = |i: usize| i as f32 * cfg::ROW_PITCH - scroll;
    for i in 0..rows.len() {
        let y = row_y(i);
        if y + cfg::ROW_HEIGHT > 0.0 && y < list_h {
            let hover = hovered == Some(i) && selected != Some(i);
            selectable(canvas, Rect { x: row_x, y, w: row_w, h: cfg::ROW_HEIGHT }, metrics::RADIUS_WIDGET, false, hover, p.surface, 1.0);
        }
    }
    if selected.is_some() {
        let r = Rect { x: row_x, y: model.highlight_offset() - scroll, w: row_w, h: cfg::ROW_HEIGHT };
        selectable(canvas, r, metrics::RADIUS_WIDGET, true, false, p.surface, 1.0);
    }
    for (i, row) in rows.iter().enumerate() {
        let y = row_y(i);
        if y + cfg::ROW_HEIGHT <= 0.0 || y >= list_h {
            continue;
        }
        if y >= 0.0 && y + cfg::ROW_HEIGHT <= list_h {
            frame.rows.push((Rect { x: content_x, y: list_top + y, w: row_w, h: cfg::ROW_HEIGHT }, i));
        }

        let mut x = row_x + cfg::PAD;
        let icon_px = cfg::ICON_SIZE as f32;
        let image = if row.icon_path.is_empty() { NO_IMAGE } else { canvas.image(&row.icon_path, cfg::ICON_SIZE) };
        if image != NO_IMAGE {
            canvas.draw_image(image, Rect { x, y: y + (cfg::ROW_HEIGHT - icon_px) / 2.0, w: icon_px, h: icon_px }, WHITE);
        } else {
            let size = canvas.measure(row.glyph, &ICON_STYLE);
            canvas.text(row.glyph, &ICON_STYLE, x + (icon_px - size.w) / 2.0, y + (cfg::ROW_HEIGHT - size.h) / 2.0, p.text);
        }
        x += icon_px + cfg::PAD;

        let label = elide(&row.label, cfg::MAX_ROW_CHARS);
        let label_size = canvas.measure(&label, &LABEL_STYLE);
        if row.subtitle.is_empty() {
            canvas.text(&label, &LABEL_STYLE, x, y + (cfg::ROW_HEIGHT - label_size.h) / 2.0, p.text);
            continue;
        }
        let subtitle = elide_middle(&row.subtitle, cfg::MAX_ROW_CHARS);
        let sub_size = canvas.measure(&subtitle, &SMALL_STYLE);
        let top = y + (cfg::ROW_HEIGHT - label_size.h - cfg::TWO_LINE_GAP - sub_size.h) / 2.0;
        canvas.text(&label, &LABEL_STYLE, x, top, p.text);
        canvas.text(&subtitle, &SMALL_STYLE, x, top + label_size.h + cfg::TWO_LINE_GAP, p.text_alpha65);
    }

    for slot in 0..cfg::MAX_VISIBLE {
        let asset = format!("{}{}{}", cfg::BULLET_ASSET_PREFIX, slot + 1, cfg::BULLET_ASSET_SUFFIX);
        let bullet = canvas.image(&asset, cfg::BULLET_SIZE as i32);
        if bullet == NO_IMAGE {
            continue;
        }
        let r = Rect {
            x: (cfg::SEARCH_HEIGHT - cfg::BULLET_SIZE) / 2.0,
            y: slot as f32 * cfg::ROW_PITCH + (cfg::ROW_HEIGHT - cfg::BULLET_SIZE) / 2.0,
            w: cfg::BULLET_SIZE,
            h: cfg::BULLET_SIZE,
        };
        canvas.draw_image(bullet, r, WHITE);
    }

    canvas.end_group();
    canvas.end_group();
    if frame.entrance.flash > 0.0 {
        canvas.set_opacity(1.0);
        canvas.rect(Rect { x: 0.0, y: 0.0, w: width, h: height }, with_alpha(p.accent_container, frame.entrance.flash));
    }
    frame
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::base::input::KeyKind;
    use crate::config::launcher_config as cfg;
    use crate::modules::launcher::entrance::EntranceState;
    use crate::modules::launcher::model::Model;
    use crate::modules::launcher::model::tests::{Snap, key, type_text};
    use crate::render::recording_canvas::{Op, OpKind, RecordingCanvas};

    #[test]
    fn launcher_view() {
        let _snap = Snap::on();
        let mut model = Model::default();
        model.open(false);
        model.set_apps(Vec::new());
        type_text(&mut model, "xy");
        model.sync_layout();
        let mut canvas = RecordingCanvas::default();
        let frame = paint(&mut canvas, &mut model, 1920.0, 1200.0, None);
        assert!(frame.rect.w == cfg::WIDTH && frame.rect.x == (1920.0 - cfg::WIDTH) / 2.0 && frame.rows.is_empty(), "the card is centered and has no rows");
        assert_eq!(frame.caret.w, cfg::CARET_WIDTH, "the caret is reported for input methods");
        assert!(canvas.count(OpKind::BeginGroup) == canvas.count(OpKind::EndGroup) && canvas.count(OpKind::BeginGroup) >= 2, "groups are balanced");
        assert!(canvas.ops.iter().any(|op| op.kind == OpKind::Text && op.text == "xy"), "the query is drawn");

        let plain = |recorded: &RecordingCanvas| -> Vec<Op> { recorded.ops.iter().filter(|op| op.kind != OpKind::Opacity).cloned().collect() };
        let finished = EntranceState { t: 1.0, seed: 3.0 };
        let mut animated = RecordingCanvas::default();
        let done = paint(&mut animated, &mut model, 1920.0, 1200.0, Some(&finished));
        let (expected, actual) = (plain(&canvas), plain(&animated));
        let identical = expected.len() == actual.len()
            && done.rect.y == frame.rect.y
            && expected.iter().zip(&actual).all(|(e, a)| e.kind == a.kind && e.rect.x == a.rect.x && e.rect.y == a.rect.y && e.text == a.text);
        assert!(identical, "a finished entrance paints the same card as none");

        let tracing = EntranceState { t: 0.2, seed: 3.0 };
        let mut start = RecordingCanvas::default();
        let early = paint(&mut start, &mut model, 1920.0, 1200.0, Some(&tracing));
        assert!(
            early.rect.y == frame.rect.y && early.entrance.opacity == 0.0 && !early.entrance.bolts.is_empty() && start.count(OpKind::Rect) == canvas.count(OpKind::Rect) + 1,
            "the card is hidden in place under a dimmed backdrop while the outline is traced"
        );
        model.close();
    }

    #[test]
    fn launcher_field_paint() {
        let _snap = Snap::on();
        let mut model = Model::default();
        model.open(false);
        model.set_apps(Vec::new());
        type_text(&mut model, "abc");
        model.sync_layout();

        let mut plain = RecordingCanvas::default();
        let frame = paint(&mut plain, &mut model, 1920.0, 1200.0, None);
        assert!(frame.field.w > 0.0 && field_hit(&frame, frame.field.x + 1.0, frame.field.y + 1.0), "the field is reported for pointer hits");
        assert!(!field_hit(&frame, frame.field.x - 1.0, frame.field.y + 1.0), "points outside the field miss");
        assert!(field_column(&frame, frame.input.text_x) == 0.0 && field_column(&frame, frame.input.text_x - 50.0) == 0.0, "columns start at the text origin");
        let caret_at_end = frame.caret.x;

        model.key(&key(KeyKind::Left, ""));
        let mut moved = RecordingCanvas::default();
        let left = paint(&mut moved, &mut model, 1920.0, 1200.0, None);
        assert!(left.caret.x < caret_at_end, "the caret follows the cursor");
        assert_eq!(moved.count(OpKind::Rect), plain.count(OpKind::Rect), "no selection draws no highlight");

        model.key(&crate::base::input::KeyEvent { shift: true, ..key(KeyKind::Left, "") });
        let mut highlighted = RecordingCanvas::default();
        paint(&mut highlighted, &mut model, 1920.0, 1200.0, None);
        assert_eq!(highlighted.count(OpKind::Rect), plain.count(OpKind::Rect) + 1, "a selection draws one highlight");
        model.close();
    }
}
