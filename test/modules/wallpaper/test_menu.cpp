#include "check.h"
#include "render/recording_canvas.h"

#include "modules/wallpaper/model.h"
#include "modules/wallpaper/view.h"

void check_desktop_menu() {
    using astralia::DesktopMenuAction;
    using astralia::DesktopMenuModel;
    DesktopMenuModel menu;
    test::check(!menu.is_open() && menu.entry_at(5, 5) == -1, "the menu starts closed");

    menu.open(100.0f, 80.0f, 800.0f, 600.0f, 1);
    test::check(menu.is_open() && menu.box().x == 100.0f && menu.box().y == 80.0f && menu.column() == 1, "the menu opens at the click");
    menu.open(790.0f, 590.0f, 800.0f, 600.0f, 0);
    test::check(menu.box().x + menu.box().w <= 800.0f && menu.box().y + menu.box().h <= 600.0f, "the menu stays inside the output");

    const auto &entries = DesktopMenuModel::entries();
    test::check(entries.size() == 2 && entries[0].action == DesktopMenuAction::settings && entries[1].action == DesktopMenuAction::wallpaper, "the menu offers settings and wallpaper");
    astralia::ui::Box first = menu.row(0);
    astralia::ui::Box second = menu.row(1);
    test::check(menu.entry_at(first.x + 1.0f, first.y + 1.0f) == 0 && menu.entry_at(second.x + 1.0f, second.y + 1.0f) == 1 && menu.entry_at(-5.0f, -5.0f) == -1, "rows are hit tested in menu coordinates");

    test::check(menu.hover(first.x + 1.0f, first.y + 1.0f) && menu.hovered() == 0, "hovering a row highlights it");
    test::check(!menu.hover(first.x + 2.0f, first.y + 2.0f), "staying on a row changes nothing");
    menu.clear_hover();
    test::check(menu.hovered() == -1, "hover clears");

    test::RecordingCanvas canvas;
    astralia::paint_desktop_menu(canvas, menu, menu.box().x, menu.box().y);
    int labels = 0;
    for (const test::Op &op : canvas.ops) {
        labels += op.kind == test::Op::Kind::text && (op.text == "Settings" || op.text == "Wallpaper");
    }
    test::check(labels == 2, "both entries are drawn");

    test::check(!menu.toggle(10.0f, 10.0f, 800.0f, 600.0f, 0) && !menu.is_open(), "toggling an open menu closes it");
    test::check(menu.toggle(10.0f, 10.0f, 800.0f, 600.0f, 0) && menu.is_open(), "toggling a closed menu opens it");
    menu.close();
    test::RecordingCanvas empty;
    astralia::paint_desktop_menu(empty, menu, 0.0f, 0.0f);
    test::check(empty.ops.empty(), "a closed menu draws nothing");
}
