#include <chrono>
#include <cmath>
#include <string>

#include "check.h"
#include "service/fake_compositor.h"
#include "ui/recording_canvas.h"

#include "config/overview_config.h"

#include "core/animation.h"

#include "modules/overview/model.h"
#include "modules/overview/view.h"

namespace {

namespace cfg = astralia::overview_config;
using astralia::OverviewModel;
using astralia::input::KeyEvent;
using astralia::input::KeyKind;

struct Instant {
    Instant() { astralia::animation_set_instant(true); }
    ~Instant() { astralia::animation_set_instant(false); }
};

KeyEvent key(KeyKind kind, std::string text = "", uint32_t base = 0, bool shift = false, bool alt = false, bool ctrl = false) {
    KeyEvent event;
    event.kind = kind;
    event.text = std::move(text);
    event.base_sym = base;
    event.shift = shift;
    event.alt = alt;
    event.ctrl = ctrl;
    return event;
}

astralia::CompositorClient client(std::string address, int workspace, double x, double y, double w, double h) {
    astralia::CompositorClient c;
    c.address = std::move(address);
    c.window_class = "app";
    c.workspace_id = workspace;
    c.monitor_id = 1;
    c.at = {x, y};
    c.size = {w, h};
    return c;
}

void setup_single(test::FakeCompositor &compositor) {
    astralia::CompositorMonitor m = test::monitor(1, "A", 0, 0, 1366, 768);
    m.reserved = {0, 0, 0, 20};
    compositor.state_.monitors = {m};
    compositor.state_.focused_monitor = "A";
    compositor.state_.by_monitor["A"].active_id = 1;
}

void check_layout() {
    Instant instant;
    test::FakeCompositor compositor;
    setup_single(compositor);
    OverviewModel model(compositor);
    model.open("A", 1366, 768);
    model.sync(1366, 768);
    const astralia::OverviewLayout &layout = model.layout();
    test::check(layout.cells.size() == 10 && layout.cells[0].workspace == 1 && layout.cells[9].workspace == 10, "ten cells in ids");
    test::check(layout.panels.size() == 1, "one panel");
    const astralia::ui::Box &panel = layout.panels[0];
    test::check(panel.x >= 0 && panel.y >= 0 && panel.x + panel.w <= 1366 && panel.y + panel.h <= 768, "the panel fits the surface");
    test::check(std::fabs(panel.x + panel.w / 2.0f - 683.0f) <= 1.0f, "the panel is centred");

    model.sync(5000, 3000);
    test::check(model.layout().scale == cfg::scale, "the scale is capped");
    model.sync(400, 300);
    test::check(model.layout().scale < cfg::scale && model.layout().panels[0].w <= 400 && model.layout().panels[0].h <= 300, "the scale shrinks to fit");
    model.sync(1366, 768);

    compositor.state_.by_monitor["A"].active_id = 12;
    model.close();
    model.open("A", 1366, 768);
    model.sync(1366, 768);
    test::check(model.layout().cells[0].workspace == 11 && model.selected() == 12, "the page follows the active workspace");

    test::FakeCompositor two;
    two.state_.monitors = {test::monitor(1, "A", 0, 0, 1920, 1080), test::monitor(2, "B", 1920, 0, 1920, 1080)};
    two.state_.by_monitor["A"].active_id = 1;
    two.state_.by_monitor["B"].active_id = 11;
    two.state_.focused_monitor = "A";
    OverviewModel global(two);
    global.open("A", 1920, 1080);
    global.key(key(KeyKind::Tab));
    test::check(global.global_mode(), "tab switches to the global view");
    global.sync(1920, 1080);
    test::check(global.layout().panels.size() == 2 && global.layout().cells.size() == 20, "one block per monitor");
    test::check(global.layout().cells[10].workspace == 11, "each block shows its own page");
    global.key(key(KeyKind::Tab));
    test::check(!global.global_mode(), "tab switches back");
}

void check_tiles() {
    Instant instant;
    test::FakeCompositor compositor;
    setup_single(compositor);
    compositor.state_.clients = {client("0x1", 1, 0, 0, 683, 374), client("0x2", 2, -500, 9000, 100, 100), client("0x3", 99, 0, 0, 10, 10)};
    compositor.state_.clients[0].floating = true;
    OverviewModel model(compositor);
    model.open("A", 1366, 768);
    model.sync(1366, 768);
    test::check(model.tiles().size() == 2, "windows on other pages get no tile");
    const astralia::OverviewCell *cell = nullptr;
    for (const auto &c : model.layout().cells) {
        if (c.workspace == 2) {
            cell = &c;
        }
    }
    test::check(cell != nullptr, "cell for workspace two");
    for (const astralia::OverviewTile &tile : model.tiles()) {
        astralia::ui::Box r = model.tile_rect(tile);
        const astralia::ui::Box &c = tile.workspace == 1 ? model.layout().cells[0].rect : cell->rect;
        test::check(r.x >= c.x - 1e-3f && r.y >= c.y - 1e-3f && r.x + r.w <= c.x + c.w + 1e-3f && r.y + r.h <= c.y + c.h + 1e-3f, "tiles stay inside their cell");
    }
    test::check(model.tiles()[0].address == "0x2", "floating windows draw on top");
}

void check_input() {
    Instant instant;
    test::FakeCompositor compositor;
    setup_single(compositor);
    compositor.state_.clients = {client("0x1", 1, 100, 100, 400, 300)};
    OverviewModel model(compositor);
    int close_requests = 0;
    int actions = 0;
    model.on_close_requested = [&] { ++close_requests; };
    model.on_compositor_action = [&] { ++actions; };
    test::check(!model.key(key(KeyKind::Right)), "a closed overview ignores keys");
    model.open("A", 1366, 768);
    model.sync(1366, 768);

    test::check(model.key(key(KeyKind::Right)) && compositor.calls.back() == "focus 2" && model.selected() == 2, "right selects the next workspace");
    test::check(model.key(key(KeyKind::Left)) && model.key(key(KeyKind::Left)) && compositor.calls.back() == "focus 5", "left wraps within the page");
    test::check(model.key(key(KeyKind::Down)) && compositor.calls.back() == "focus 10", "down changes the row");
    test::check(model.key(key(KeyKind::Right, "", 0, true)) && compositor.calls.back() == "swap 6", "shift moves the workspace contents");
    test::check(model.key(key(KeyKind::Right, "", 0, false, true)) && compositor.calls.back() == "move-in 7", "alt moves the workspace into the monitor");
    test::check(model.key(key(KeyKind::Text, "3", '3')) && compositor.calls.back() == "focus 3", "digits pick a workspace on the page");
    test::check(model.key(key(KeyKind::Text, "0", '0')) && compositor.calls.back() == "focus 10", "zero is the tenth");
    test::check(model.key(key(KeyKind::Text, "#", '3', true)) && compositor.calls.back() == "swap 3", "shift digit uses the base symbol");
    test::check(actions >= 8, "every compositor command is announced first");
    test::check(model.key(key(KeyKind::Text, "d", 'd')) && compositor.calls.back() == "close-windows workspace 3", "d closes the workspace");
    test::check(model.key(key(KeyKind::Text, "D", 'd', true)) && compositor.calls.back() == "close-windows monitor 1", "shift d closes the monitor");
    test::check(model.key(key(KeyKind::Text, "d", 'd', false, false, true)) && compositor.calls.back() == "close-windows all -1", "ctrl d closes everything");
    test::check(!model.key(key(KeyKind::Text, "x", 'x')), "other text is ignored");
    test::check(model.key(key(KeyKind::Escape)) && close_requests == 1, "escape asks to close");

    model.sync(1366, 768);
    const astralia::OverviewCell &fourth = model.layout().cells[3];
    model.press(fourth.rect.x + 2, fourth.rect.y + 2);
    test::check(compositor.calls.back() == "focus 4", "clicking a cell focuses it");
    model.press(1, 1);
    test::check(close_requests == 2, "clicking outside closes");

    astralia::ui::Box tile = model.tile_rect(model.tiles()[0]);
    model.press(tile.x + 2, tile.y + 2);
    test::check(model.dragging() && model.clickable(tile.x + 2, tile.y + 2), "pressing a tile starts a drag");
    const astralia::OverviewCell &target = model.layout().cells[6];
    model.move(target.rect.x + 3, target.rect.y + 3);
    test::check(model.drag_target() == 7, "the cell under the pointer is the drop target");
    model.sync(1366, 768);
    test::check(model.tile_rect(model.tiles().back()).x == static_cast<float>(target.rect.x + 3 - (2)), "the dragged tile follows the pointer");
    model.release();
    test::check(!model.dragging() && compositor.calls.back() == "move 0x1 7", "dropping on another workspace moves the window");

    model.press(tile.x + 2, tile.y + 2);
    model.move(tile.x + 4, tile.y + 4);
    model.release();
    test::check(compositor.calls.back() == "focus 1", "dropping on the same workspace focuses it");
}

void check_slide() {
    test::FakeCompositor compositor;
    setup_single(compositor);
    OverviewModel model(compositor);
    int closed = 0;
    model.on_closed = [&] { ++closed; };
    model.open("A", 1366, 768);
    test::check(model.slide_y() == 768.0f && model.animating(), "the overview starts below the screen");
    model.tick(std::chrono::steady_clock::now() + std::chrono::seconds(1));
    test::check(model.slide_y() == 0.0f, "and slides up");
    model.sync(1366, 768);
    test::check(model.indicator() != nullptr, "the indicator shows once settled");
    model.close();
    test::check(model.is_open(), "closing keeps the overview until the slide ends");
    test::check(!model.key(key(KeyKind::Right)), "keys are ignored while closing");
    model.tick(std::chrono::steady_clock::now() + std::chrono::seconds(2));
    test::check(!model.is_open() && closed == 1, "the overview closes after the slide");
}

void check_view() {
    using Kind = test::Op::Kind;
    Instant instant;
    test::FakeCompositor compositor;
    setup_single(compositor);
    compositor.state_.clients = {client("0x1", 1, 0, 0, 683, 374)};
    OverviewModel model(compositor);
    model.open("A", 1366, 768);
    model.sync(1366, 768);
    test::RecordingCanvas canvas;
    astralia::paint_overview(canvas, model, {});
    test::check(canvas.count(Kind::rounded) == 1 + 10 + 1 + 1, "panel, cells, one tile and the indicator");
    test::check(canvas.count(Kind::text) == 10 && canvas.count(Kind::image) <= 1, "a number per cell");

    test::RecordingCanvas live;
    int art_calls = 0;
    astralia::paint_overview(live, model, [&](astralia::ui::Canvas &, const astralia::OverviewTile &, const astralia::ui::Box &, float) {
        ++art_calls;
        return true;
    });
    test::check(art_calls == 1 && live.count(Kind::rounded) == 1 + 10 + 1, "live art replaces the placeholder tile");
}

} // namespace

void check_overview() {
    check_layout();
    check_tiles();
    check_input();
    check_slide();
    check_view();
}
