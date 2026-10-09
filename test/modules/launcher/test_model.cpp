#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "check.h"
#include "core/pump.h"
#include "render/recording_canvas.h"

#include "config/launcher_config.h"

#include "core/animation.h"
#include "core/poll_reactor.h"

#include "modules/launcher/model.h"
#include "modules/launcher/view.h"

namespace {

namespace cfg = astralia::launcher_config;
using astralia::LauncherMode;
using astralia::LauncherModel;
using astralia::input::KeyEvent;
using astralia::input::KeyKind;

struct Instant {
    Instant() { astralia::animation_set_instant(true); }
    ~Instant() { astralia::animation_set_instant(false); }
};

KeyEvent key(KeyKind kind, std::string text = "") {
    KeyEvent event;
    event.kind = kind;
    event.text = std::move(text);
    return event;
}

void type(LauncherModel &model, const std::string &text) {
    for (char c : text) {
        model.key(key(KeyKind::Text, std::string(1, c)));
    }
}

astralia::PollReactor make_reactor() {
    auto made = astralia::PollReactor::create();
    return std::move(*made);
}

void check_modes_and_layout() {
    Instant instant;
    astralia::PollReactor reactor = make_reactor();
    LauncherModel model(reactor);
    model.open(false);
    int changes = 0;
    model.on_changed = [&] { ++changes; };
    model.set_search_root("/nonexistent-root");
    model.set_apps({{"firefox.desktop", "Firefox", "firefox", "", false, false, false}});

    type(model, "gg cats");
    test::check(model.mode() == LauncherMode::google && model.field().text == "gg cats", "a prefix switches the mode");
    test::check(changes == 7, "each key reports a change");
    model.key(key(KeyKind::Backspace));
    model.key(key(KeyKind::Backspace));
    test::check(model.field().text == "gg ca", "backspace removes whole characters");
    test::check(model.query_anim().chars.size() == 5, "the typing animation follows the text");

    model.hover(2);
    model.key(key(KeyKind::Up));
    test::check(model.hovered() == -1, "a key press clears the hover");

    model.sync_layout();
    test::check(model.box_height() == astralia::launcher_content_height(0), "an empty list shows only the search row");
    test::check(astralia::launcher_content_height(6) == cfg::menu_pad * 2 + cfg::search_height + cfg::list_gap + 6 * cfg::row_height + 5 * cfg::row_spacing, "full list height");

    model.set_preedit("ni");
    test::check(model.field().preedit == "ni", "preedit is kept apart from the text");
    model.commit_text("x");
    test::check(model.field().text == "gg cax" && model.field().preedit.empty(), "committing appends and drops the preedit");
    model.delete_before(2);
    test::check(model.field().text == "gg c", "deleting before the cursor removes characters");
    model.toggle_caret();
    test::check(!model.field().cursor_idle_visible, "the caret blink toggles");
    model.key(key(KeyKind::Text, "a"));
    test::check(model.field().cursor_idle_visible, "typing shows the caret");

    int closes = 0;
    model.on_close_requested = [&] { ++closes; };
    model.key(key(KeyKind::Escape));
    test::check(closes == 1, "escape on the main screen asks to close");
    model.key(key(KeyKind::Left));
    model.key(key(KeyKind::Right));
    test::check(closes == 1, "other keys do nothing");
}

void check_search_flow() {
    if (std::system("command -v fd >/dev/null 2>&1") != 0) {
        return;
    }
    Instant instant;
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("astralia_test_model_" + std::to_string(getpid()));
    std::filesystem::create_directories(root / "firefoxdir");
    std::ofstream(root / "firefox_notes.txt").put('x');

    astralia::PollReactor reactor = make_reactor();
    LauncherModel model(reactor);
    model.open(false);
    model.set_search_root(root.string());
    model.set_apps({{"firefox.desktop", "Firefox", "firefox", "", false, false, false}});
    bool changed = false;
    model.on_changed = [&] { changed = true; };

    type(model, "firefox");
    changed = false;
    test::check(test::pump_until(reactor, [&] { return !model.searching() && model.results().size() == 3; }), "the search finishes");
    test::check(changed, "finishing the search reports a change");
    const auto &results = model.results();
    test::check(results.size() == 3 && results[0].kind == astralia::DrunResult::Kind::app, "the app comes first");
    test::check(results[1].kind == astralia::DrunResult::Kind::dir && results[1].file.name == "firefoxdir", "then the directory");
    test::check(results[2].kind == astralia::DrunResult::Kind::file && results[2].file.name == "firefox_notes.txt", "then the file");
    test::check(model.selected() == 0, "the first result is selected");

    std::vector<astralia::LauncherRow> rows = model.rows();
    test::check(rows.size() == 3 && rows[0].label == "Firefox" && rows[1].subtitle.find("firefoxdir") != std::string::npos, "rows describe the results");

    model.key(key(KeyKind::Down));
    model.key(key(KeyKind::Down));
    model.key(key(KeyKind::Down));
    test::check(model.selected() == 2, "selection stops at the last row");
    model.key(key(KeyKind::Up));
    test::check(model.selected() == 1, "up moves back");
    model.key(key(KeyKind::Enter));
    test::check(model.submenu().screen == astralia::SubmenuScreen::browse && model.selected() == 0, "enter on a directory opens it");
    test::check(!model.submenu().items.empty(), "the directory listing has entries");
    model.key(key(KeyKind::Escape));
    test::check(model.submenu().screen == astralia::SubmenuScreen::search && model.selected() == 0, "escape goes back to the results");
    model.click_row(2);
    test::check(model.submenu().screen == astralia::SubmenuScreen::file_actions && model.selected() == 0, "clicking a file opens its actions");
    model.key(key(KeyKind::Text, "z"));
    test::check(model.submenu().screen == astralia::SubmenuScreen::search, "typing leaves the submenu");

    for (int i = 0; i < 8; ++i) {
        model.key(key(KeyKind::Backspace));
    }
    test::check(test::pump_until(reactor, [&] { return model.results().empty(); }), "an empty query clears the results");
    test::check(model.selected() == -1, "and the selection");

    model.close();
    test::check(!model.is_open() && model.field().text.empty() && model.results().empty() && model.submenu().screen == astralia::SubmenuScreen::search, "closing resets everything");
    std::filesystem::remove_all(root);
}

void check_layout_animation() {
    Instant instant;
    astralia::PollReactor reactor = make_reactor();
    LauncherModel model(reactor);
    model.sync_layout();
    float closed = model.box_height();
    test::check(closed == astralia::launcher_content_height(0), "starts at the search row");
    test::check(model.highlight_offset() == 0.0f && model.scroll_offset() == 0.0f, "nothing selected, nothing offset");
}

void check_view() {
    using Kind = test::Op::Kind;
    Instant instant;
    astralia::PollReactor reactor = make_reactor();
    LauncherModel model(reactor);
    model.open(false);
    model.set_apps({});
    type(model, "xy");
    model.sync_layout();
    test::RecordingCanvas canvas;
    astralia::LauncherFrame frame = astralia::paint_launcher(canvas, model, 1920, 1200);
    test::check(frame.box.w == cfg::width && frame.box.x == (1920 - cfg::width) / 2 && frame.rows.empty(), "the card is centered and has no rows");
    test::check(frame.caret.w == cfg::caret_width, "the caret is reported for input methods");
    test::check(canvas.count(Kind::begin_group) == canvas.count(Kind::end_group) && canvas.count(Kind::begin_group) >= 2, "groups are balanced");
    bool query = false;
    for (const test::Op &op : canvas.ops) {
        query = query || (op.kind == Kind::text && op.text == "xy");
    }
    test::check(query, "the query is drawn");
    model.close();
}

} // namespace

void check_launcher_model() {
    check_modes_and_layout();
    check_search_flow();
    check_layout_animation();
    check_view();
}
