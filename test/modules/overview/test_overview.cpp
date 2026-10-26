#include <chrono>
#include <cmath>
#include <string>

#include "config/overview_config.h"

#include "core/animation.h"

#include "modules/overview/model.h"
#include "modules/overview/view.h"

#include "render/scroll.h"

#include "check.h"
#include "render/recording_canvas.h"
#include "service/fake_compositor.h"

namespace {

namespace cfg = astralia::overview_config;
using astralia::input::KeyEvent;
using astralia::input::KeyKind;
using astralia::overview::Model;

struct Instant {
    Instant() { astralia::animation_set_instant(true); }
    ~Instant() { astralia::animation_set_instant(false); }
};

KeyEvent key(KeyKind kind, std::string text = "", uint32_t base = 0, bool shift = false,
             bool alt = false, bool ctrl = false) {
    KeyEvent event;
    event.kind = kind;
    event.text = std::move(text);
    event.base_sym = base;
    event.shift = shift;
    event.alt = alt;
    event.ctrl = ctrl;
    return event;
}

astralia::CompositorClient client(std::string address, int workspace, double x, double y, double w,
                                  double h) {
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
    astralia::overview::Model model(compositor);
    model.open("A", 1366, 768);
    model.sync(1366, 768);
    const astralia::overview::Layout &layout = model.layout();
    test::check(layout.cells.size() == 5 && layout.cells[0].workspace == 1 &&
                    layout.cells[4].workspace == 5,
                "five cells in ids");
    test::check(layout.panels.size() == 1, "one panel");
    const astralia::ui::Box &panel = layout.panels[0];
    test::check(panel.x >= 0 && panel.y >= 0 && panel.x + panel.w <= 1366 &&
                    panel.y + panel.h <= 768,
                "the panel fits the surface");
    test::check(std::fabs(panel.x + panel.w / 2.0f - 683.0f) <= 1.0f, "the panel is centred");

    model.sync(5000, 3000);
    test::check(model.layout().scale == cfg::scale, "the scale is capped");
    model.sync(400, 300);
    test::check(model.layout().scale < cfg::scale && model.layout().panels[0].w <= 400 &&
                    model.layout().panels[0].h <= 300,
                "the scale shrinks to fit");
    model.sync(1366, 768);

    compositor.state_.by_monitor["A"].active_id = 12;
    model.close();
    model.open("A", 1366, 768);
    model.sync(1366, 768);
    test::check(model.layout().cells[0].workspace == 11 && model.selected() == 12,
                "the page follows the active workspace");

    test::FakeCompositor two;
    two.state_.monitors = {test::monitor(1, "A", 0, 0, 1920, 1080),
                           test::monitor(2, "B", 1920, 0, 1920, 1080)};
    two.state_.by_monitor["A"].active_id = 1;
    two.state_.by_monitor["B"].active_id = 11;
    two.state_.focused_monitor = "A";
    astralia::overview::Model global(two);
    global.open("A", 1920, 1080);
    global.key(key(KeyKind::Tab));
    test::check(global.global_mode(), "tab switches to the global view");
    global.sync(1920, 1080);
    test::check(global.layout().panels.size() == 2 && global.layout().cells.size() == 10,
                "one block per monitor");
    test::check(global.layout().cells[5].workspace == 11, "each block shows its own page");
    global.key(key(KeyKind::Tab));
    test::check(!global.global_mode(), "tab switches back");
}

void check_tiles() {
    Instant instant;
    test::FakeCompositor compositor;
    setup_single(compositor);
    compositor.state_.clients = {client("0x1", 1, 0, 0, 683, 374),
                                 client("0x2", 2, -500, 9000, 100, 100),
                                 client("0x3", 99, 0, 0, 10, 10)};
    compositor.state_.clients[0].floating = true;
    astralia::overview::Model model(compositor);
    model.open("A", 1366, 768);
    model.sync(1366, 768);
    test::check(model.tiles().size() == 2, "windows on other pages get no tile");
    const astralia::overview::Cell *cell = nullptr;
    for (const auto &c : model.layout().cells) {
        if (c.workspace == 2) {
            cell = &c;
        }
    }
    test::check(cell != nullptr, "cell for workspace two");
    for (const astralia::overview::Tile &tile : model.tiles()) {
        astralia::ui::Box r = model.tile_rect(tile);
        const astralia::ui::Box &c =
            tile.workspace == 1 ? model.layout().cells[0].rect : cell->rect;
        test::check(r.x >= c.x - 1e-3f && r.y >= c.y - 1e-3f && r.x + r.w <= c.x + c.w + 1e-3f &&
                        r.y + r.h <= c.y + c.h + 1e-3f,
                    "tiles stay inside their cell");
    }
    test::check(model.tiles()[0].address == "0x2", "floating windows draw on top");
}

void check_input() {
    Instant instant;
    test::FakeCompositor compositor;
    setup_single(compositor);
    compositor.state_.clients = {client("0x1", 1, 100, 100, 400, 300)};
    astralia::overview::Model model(compositor);
    int close_requests = 0;
    model.on_close_requested = [&] { ++close_requests; };
    test::check(!model.key(key(KeyKind::Right)), "a closed overview ignores keys");
    model.open("A", 1366, 768);
    model.sync(1366, 768);

    test::check(model.key(key(KeyKind::Right)) && compositor.calls.back() == "focus 2" &&
                    model.selected() == 2,
                "right selects the next workspace");
    test::check(model.key(key(KeyKind::Left)) && model.key(key(KeyKind::Left)) &&
                    compositor.calls.back() == "focus 5",
                "left wraps within the page");
    test::check(model.key(key(KeyKind::Down)) && compositor.calls.back() == "focus 5",
                "down stays on the only row");
    test::check(model.key(key(KeyKind::Right, "", 0, true)) && compositor.calls.back() == "swap 1",
                "shift moves the workspace contents");
    test::check(model.key(key(KeyKind::Right, "", 0, false, true)) &&
                    compositor.calls.back() == "move-in 2",
                "alt moves the workspace into the monitor");
    test::check(model.key(key(KeyKind::Text, "3", '3')) && compositor.calls.back() == "focus 3",
                "digits pick a workspace on the page");
    test::check(model.key(key(KeyKind::Text, "0", '0')) && compositor.calls.back() == "focus 3",
                "zero is past the five and does nothing");
    test::check(model.key(key(KeyKind::Text, "#", '3', true)) &&
                    compositor.calls.back() == "swap 3",
                "shift digit uses the base symbol");
    test::check(model.key(key(KeyKind::Text, "d", 'd')) &&
                    compositor.calls.back() == "close-windows workspace 3",
                "d closes the workspace");
    test::check(model.key(key(KeyKind::Text, "D", 'd', true)) &&
                    compositor.calls.back() == "close-windows monitor 1",
                "shift d closes the monitor");
    test::check(model.key(key(KeyKind::Text, "d", 'd', false, false, true)) &&
                    compositor.calls.back() == "close-windows all -1",
                "ctrl d closes everything");
    test::check(!model.key(key(KeyKind::Text, "x", 'x')), "other text is ignored");
    test::check(model.key(key(KeyKind::Escape)) && close_requests == 1, "escape asks to close");

    model.sync(1366, 768);
    const astralia::overview::Cell &fourth = model.layout().cells[3];
    model.press(fourth.rect.x + 2, fourth.rect.y + 2);
    test::check(compositor.calls.back() == "focus 4", "clicking a cell focuses it");
    model.press(fourth.rect.x + 2, fourth.rect.y + 2, {.shift = true});
    test::check(compositor.calls.back() == "swap 4", "shift click swaps the workspace");
    model.press(fourth.rect.x + 2, fourth.rect.y + 2, {.alt = true});
    test::check(compositor.calls.back() == "move-in 4", "alt click moves the workspace in");
    model.press(1, 1);
    test::check(close_requests == 2, "clicking outside closes");

    astralia::ui::Box tile = model.tile_rect(model.tiles()[0]);
    model.press(tile.x + 2, tile.y + 2);
    test::check(model.dragging() && model.clickable(tile.x + 2, tile.y + 2),
                "pressing a tile starts a drag");
    const astralia::overview::Cell &target = model.layout().cells[4];
    model.move(target.rect.x + 3, target.rect.y + 3);
    test::check(model.drag_target() == 5, "the cell under the pointer is the drop target");
    model.sync(1366, 768);
    test::check(model.tile_rect(model.tiles().back()).x ==
                    static_cast<float>(target.rect.x + 3 - (2)),
                "the dragged tile follows the pointer");
    model.release();
    test::check(!model.dragging() && compositor.calls.back() == "move 0x1 5",
                "dropping on another workspace moves the window");

    model.press(tile.x + 2, tile.y + 2);
    model.move(tile.x + 4, tile.y + 4);
    model.release();
    test::check(compositor.calls.back() == "focus 1", "dropping on the same workspace focuses it");
}

void check_overview_stages() {
    using std::chrono::seconds;
    using std::chrono::steady_clock;
    test::FakeCompositor compositor;
    setup_single(compositor);
    compositor.state_.clients = {client("0x1", 1, 100, 100, 400, 300)};
    astralia::overview::Model model(compositor);
    int closed = 0;
    model.on_closed = [&] { ++closed; };
    model.open("A", 1366, 768);
    test::check(model.slide_y() == 768.0f && model.scroll() == 0.0f && model.animating(),
                "the closed scroll starts below the screen");
    model.sync(1366, 768);
    test::check(model.indicator() == nullptr && !model.clickable(683, 384),
                "nothing reacts while the scroll is closed");
    int calls = static_cast<int>(compositor.calls.size());
    model.press(683, 384);
    test::check(static_cast<int>(compositor.calls.size()) == calls && !model.dragging(),
                "pointer presses are ignored until the scroll is open");

    model.tick(steady_clock::now() + seconds(1));
    test::check(model.slide_y() == 0.0f && model.scroll() == 0.0f && !model.fully_open(),
                "the scroll rises first and is still closed");
    model.tick(steady_clock::now() + seconds(2));
    test::check(model.scroll() == 1.0f && model.grid_alpha() == 0.0f && model.fully_open() &&
                    model.animating(),
                "then it unrolls and the grid starts to fade in");
    model.tick(steady_clock::now() + seconds(3));
    test::check(model.grid_alpha() == 1.0f && !model.animating(), "the grid settles last");
    model.sync(1366, 768);
    test::check(model.indicator() != nullptr, "the indicator shows once settled");

    model.close();
    test::check(model.is_open() && !model.fully_open(),
                "closing keeps the overview until the drop ends");
    test::check(!model.key(key(KeyKind::Right)), "keys are ignored while closing");
    model.tick(steady_clock::now() + seconds(4));
    test::check(model.grid_alpha() == 0.0f && model.scroll() == 1.0f,
                "the grid fades out before the scroll moves");
    model.tick(steady_clock::now() + seconds(5));
    test::check(model.scroll() == 0.0f && model.slide_y() == 0.0f && model.is_open() && closed == 0,
                "the scroll rolls up before it drops");
    model.tick(steady_clock::now() + seconds(6));
    test::check(!model.is_open() && closed == 1, "the overview closes after the drop");

    model.open("A", 1366, 768);
    model.close();
    model.tick(steady_clock::now() + seconds(1));
    model.tick(steady_clock::now() + seconds(2));
    test::check(!model.is_open() && closed == 2 && model.scroll() == 0.0f,
                "closing during the rise never unrolls the scroll");

    {
        Instant instant;
        astralia::overview::Model snap(compositor);
        int snapped = 0;
        snap.on_closed = [&] { ++snapped; };
        snap.open("A", 1366, 768);
        test::check(snap.fully_open() && !snap.animating(),
                    "instant mode opens straight to the final scroll");
        snap.close();
        test::check(!snap.is_open() && snapped == 1, "and closes straight away");
    }
}

void check_view() {
    using Kind = test::Op::Kind;
    Instant instant;
    test::FakeCompositor compositor;
    setup_single(compositor);
    compositor.state_.clients = {client("0x1", 1, 0, 0, 683, 374)};
    astralia::overview::Model model(compositor);
    model.open("A", 1366, 768);
    model.sync(1366, 768);
    astralia::ScrollMetrics metrics;
    test::RecordingCanvas frame;
    astralia::paint_scroll(
        frame, astralia::scroll_geometry(model.layout().panels[0], 1.0f, metrics), metrics);
    size_t scroll_ops = frame.count(Kind::rounded);

    test::RecordingCanvas canvas;
    astralia::overview::paint(canvas, model, {});
    test::check(canvas.count(Kind::rounded) == scroll_ops + 5 + 1 + 2,
                "the scroll, cells, one tile and the indicator with its shadow");
    test::check(canvas.count(Kind::text) == 5 && canvas.count(Kind::image) <= 1,
                "a number per cell");

    test::RecordingCanvas live;
    int art_calls = 0;
    astralia::overview::paint(live, model,
                              [&](astralia::ui::Canvas &, const astralia::overview::Tile &,
                                  const astralia::ui::Box &, float) {
                                  ++art_calls;
                                  return true;
                              });
    test::check(art_calls == 1 && live.count(Kind::rounded) == scroll_ops + 5 + 2,
                "live art replaces the placeholder tile");
}

void check_staged_view() {
    using Kind = test::Op::Kind;
    using std::chrono::milliseconds;
    using std::chrono::seconds;
    using std::chrono::steady_clock;
    test::FakeCompositor compositor;
    setup_single(compositor);
    compositor.state_.clients = {client("0x1", 1, 0, 0, 683, 374)};
    astralia::overview::Model model(compositor);
    model.open("A", 1366, 768);
    model.sync(1366, 768);
    test::RecordingCanvas rising;
    astralia::overview::paint(rising, model, {});
    test::check(rising.count(Kind::rounded) > 0 && rising.count(Kind::text) == 0,
                "the closed scroll rises with no grid");

    model.tick(steady_clock::now() + seconds(1));
    model.tick(steady_clock::now() +
               milliseconds(static_cast<int>(astralia::ScrollTimings{}.scroll_ms / 2)));
    model.sync(1366, 768);
    test::check(model.scroll() > 0.0f && model.scroll() < 1.0f, "the scroll is half unrolled");
    test::RecordingCanvas unrolling;
    astralia::overview::paint(unrolling, model, {});
    test::check(unrolling.count(Kind::text) == 0 && unrolling.count(Kind::begin_group) == 0,
                "the scroll unrolls empty");

    model.tick(steady_clock::now() + seconds(2));
    model.sync(1366, 768);
    test::RecordingCanvas waiting;
    astralia::overview::paint(waiting, model, {});
    test::check(model.scroll() == 1.0f && waiting.count(Kind::text) == 0,
                "the grid waits for the open scroll");

    model.tick(steady_clock::now() + seconds(3));
    model.sync(1366, 768);
    test::RecordingCanvas open;
    astralia::overview::paint(open, model, {});
    test::check(open.count(Kind::text) == 5 && open.count(Kind::begin_group) == 0,
                "the grid appears once the scroll is open");

    model.close();
    model.tick(steady_clock::now() +
               milliseconds(static_cast<int>(astralia::ScrollTimings{}.grid_out_ms / 2)));
    model.sync(1366, 768);
    test::RecordingCanvas fading;
    astralia::overview::paint(fading, model, {});
    test::check(model.grid_alpha() > 0.0f && model.grid_alpha() < 1.0f && model.scroll() == 1.0f &&
                    fading.count(Kind::text) == 5,
                "the grid fades while the scroll stays open");

    model.tick(steady_clock::now() + seconds(4));
    model.sync(1366, 768);
    test::RecordingCanvas gone;
    astralia::overview::paint(gone, model, {});
    test::check(gone.count(Kind::text) == 0 && model.scroll() == 1.0f,
                "the grid is gone before the scroll rolls up");
}

} // namespace

void check_overview() {
    check_layout();
    check_tiles();
    check_input();
    check_overview_stages();
    check_view();
    check_staged_view();
}
