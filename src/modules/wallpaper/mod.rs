//! The per-monitor wallpaper and its desktop menu.

pub mod model;
pub mod view;
pub mod wayland;

#[cfg(test)]
mod tests {
    use super::model::{DesktopMenuAction, DesktopMenuModel};
    use super::view::paint;
    use crate::render::recording_canvas::{OpKind, RecordingCanvas};

    #[test]
    fn desktop_menu() {
        let mut menu = DesktopMenuModel::default();
        assert!(!menu.is_open() && menu.entry_at(5.0, 5.0).is_none(), "the menu starts closed");

        menu.open(100.0, 80.0, 800.0, 600.0, 1);
        assert!(
            menu.is_open() && menu.rect().x == 100.0 && menu.rect().y == 80.0 && menu.column() == 1,
            "the menu opens at the click"
        );
        menu.open(790.0, 590.0, 800.0, 600.0, 0);
        assert!(menu.rect().x + menu.rect().w <= 800.0 && menu.rect().y + menu.rect().h <= 600.0, "the menu stays inside the output");

        let entries = DesktopMenuModel::entries();
        assert!(
            entries.len() == 2 && entries[0].action == DesktopMenuAction::Settings && entries[1].action == DesktopMenuAction::Wallpaper,
            "the menu offers settings and wallpaper"
        );
        let (first, second) = (menu.row(0), menu.row(1));
        assert!(
            menu.entry_at(first.x + 1.0, first.y + 1.0) == Some(0)
                && menu.entry_at(second.x + 1.0, second.y + 1.0) == Some(1)
                && menu.entry_at(-5.0, -5.0).is_none(),
            "rows are hit tested in menu coordinates"
        );

        assert!(menu.hover(first.x + 1.0, first.y + 1.0) && menu.hovered() == Some(0), "hovering a row highlights it");
        assert!(!menu.hover(first.x + 2.0, first.y + 2.0), "staying on a row changes nothing");
        menu.clear_hover();
        assert_eq!(menu.hovered(), None, "hover clears");

        let mut canvas = RecordingCanvas::default();
        paint(&mut canvas, &menu, menu.rect().x, menu.rect().y);
        let labels = canvas.ops.iter().filter(|op| op.kind == OpKind::Text && (op.text == "Settings" || op.text == "Wallpaper")).count();
        assert_eq!(labels, 2, "both entries are drawn");

        assert!(!menu.toggle(10.0, 10.0, 800.0, 600.0, 0) && !menu.is_open(), "toggling an open menu closes it");
        assert!(menu.toggle(10.0, 10.0, 800.0, 600.0, 0) && menu.is_open(), "toggling a closed menu opens it");
        menu.close();
        let mut empty = RecordingCanvas::default();
        paint(&mut empty, &menu, 0.0, 0.0);
        assert!(empty.ops.is_empty(), "a closed menu draws nothing");
    }

    #[test]
    fn wallpaper_view_ops() {
        let mut menu = DesktopMenuModel::default();
        menu.open(10.0, 10.0, 800.0, 600.0, 1);
        menu.hover(menu.row(1).x + 1.0, menu.row(1).y + 1.0);
        let mut canvas = RecordingCanvas::default();
        paint(&mut canvas, &menu, 10.0, 10.0);
        let kinds: Vec<OpKind> = canvas.ops.iter().map(|op| op.kind).collect();
        assert_eq!(kinds.first(), Some(&OpKind::Opacity), "the menu resets the opacity first");
        let texts: Vec<&str> = canvas.ops.iter().filter(|op| op.kind == OpKind::Text).map(|op| op.text.as_str()).collect();
        assert_eq!(
            texts,
            [crate::render::icons::SETTINGS, "Settings", crate::render::icons::PALETTE_ICON, "Wallpaper"],
            "each row draws its icon, then its label, in entry order"
        );
        let first_text = canvas.ops.iter().position(|op| op.kind == OpKind::Text).unwrap();
        assert!(canvas.ops[..first_text].iter().any(|op| op.kind == OpKind::Rounded), "the card is drawn under the rows");
    }
}
