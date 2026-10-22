#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <unistd.h>
#include <vector>

#include "config/launcher_config.h"

#include "core/animation.h"
#include "core/poll_reactor.h"

#include "modules/launcher/model.h"
#include "modules/launcher/view.h"

#include "check.h"
#include "core/pump.h"
#include "render/recording_canvas.h"

namespace {

namespace cfg = astralia::launcher_config;
using astralia::LauncherMode;
using astralia::input::KeyEvent;
using astralia::input::KeyKind;
using astralia::launcher::Model;

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

void type(astralia::launcher::Model &model, const std::string &text) {
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
    astralia::launcher::Model model(reactor);
    model.open(false);
    int changes = 0;
    model.on_changed = [&] { ++changes; };
    model.set_search_root("/nonexistent-root");
    model.set_apps({{"firefox.desktop", "Firefox", "firefox", "", false, false, false}});

    type(model, "gg cats");
    test::check(model.mode() == LauncherMode::google && model.field().text == "gg cats",
                "a prefix switches the mode");
    test::check(changes == 7, "each key reports a change");
    model.key(key(KeyKind::Backspace));
    model.key(key(KeyKind::Backspace));
    test::check(model.field().text == "gg ca", "backspace removes whole characters");
    test::check(model.query_anim().chars.size() == 5, "the typing animation follows the text");

    model.hover(2);
    model.key(key(KeyKind::Up));
    test::check(model.hovered() == -1, "a key press clears the hover");

    model.sync_layout();
    test::check(model.box_height() == astralia::launcher::content_height(0),
                "an empty list shows only the search row");
    test::check(astralia::launcher::content_height(6) == cfg::menu_pad * 2 + cfg::search_height +
                                                             cfg::list_gap + 6 * cfg::row_height +
                                                             5 * cfg::row_spacing,
                "full list height");

    model.set_preedit("ni");
    test::check(model.field().preedit == "ni", "preedit is kept apart from the text");
    model.commit_text("x");
    test::check(model.field().text == "gg cax" && model.field().preedit.empty(),
                "committing appends and drops the preedit");
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
    std::filesystem::path root = std::filesystem::temp_directory_path() /
                                 ("astralia_test_model_" + std::to_string(getpid()));
    std::filesystem::create_directories(root / "firefoxdir");
    std::ofstream(root / "firefox_notes.txt").put('x');

    astralia::PollReactor reactor = make_reactor();
    astralia::launcher::Model model(reactor);
    model.open(false);
    model.set_search_root(root.string());
    model.set_apps({{"firefox.desktop", "Firefox", "firefox", "", false, false, false}});
    bool changed = false;
    model.on_changed = [&] { changed = true; };

    type(model, "firefox");
    changed = false;
    test::check(test::pump_until(reactor,
                                 [&] { return !model.searching() && model.results().size() == 3; }),
                "the search finishes");
    test::check(changed, "finishing the search reports a change");
    const auto &results = model.results();
    test::check(results.size() == 3 && results[0].kind == astralia::DrunResult::Kind::app,
                "the app comes first");
    test::check(results[1].kind == astralia::DrunResult::Kind::dir &&
                    results[1].file.name == "firefoxdir",
                "then the directory");
    test::check(results[2].kind == astralia::DrunResult::Kind::file &&
                    results[2].file.name == "firefox_notes.txt",
                "then the file");
    test::check(model.selected() == 0, "the first result is selected");

    std::vector<astralia::launcher::Row> rows = model.rows();
    test::check(rows.size() == 3 && rows[0].label == "Firefox" &&
                    rows[1].subtitle.find("firefoxdir") != std::string::npos,
                "rows describe the results");

    model.key(key(KeyKind::Down));
    model.key(key(KeyKind::Down));
    model.key(key(KeyKind::Down));
    test::check(model.selected() == 2, "selection stops at the last row");
    model.key(key(KeyKind::Up));
    test::check(model.selected() == 1, "up moves back");
    model.key(key(KeyKind::Enter));
    test::check(model.submenu().screen == astralia::SubmenuScreen::browse && model.selected() == 0,
                "enter on a directory opens it");
    test::check(!model.submenu().items.empty(), "the directory listing has entries");
    model.key(key(KeyKind::Escape));
    test::check(model.submenu().screen == astralia::SubmenuScreen::search && model.selected() == 0,
                "escape goes back to the results");
    model.click_row(2);
    test::check(model.submenu().screen == astralia::SubmenuScreen::file_actions &&
                    model.selected() == 0,
                "clicking a file opens its actions");
    model.key(key(KeyKind::Text, "z"));
    test::check(model.submenu().screen == astralia::SubmenuScreen::search,
                "typing leaves the submenu");

    for (int i = 0; i < 8; ++i) {
        model.key(key(KeyKind::Backspace));
    }
    test::check(test::pump_until(reactor, [&] { return model.results().empty(); }),
                "an empty query clears the results");
    test::check(model.selected() == -1, "and the selection");

    model.close();
    test::check(!model.is_open() && model.field().text.empty() && model.results().empty() &&
                    model.submenu().screen == astralia::SubmenuScreen::search,
                "closing resets everything");
    std::filesystem::remove_all(root);
}

void check_layout_animation() {
    Instant instant;
    astralia::PollReactor reactor = make_reactor();
    astralia::launcher::Model model(reactor);
    model.sync_layout();
    float closed = model.box_height();
    test::check(closed == astralia::launcher::content_height(0), "starts at the search row");
    test::check(model.highlight_offset() == 0.0f && model.scroll_offset() == 0.0f,
                "nothing selected, nothing offset");
}

void check_view() {
    using Kind = test::Op::Kind;
    Instant instant;
    astralia::PollReactor reactor = make_reactor();
    astralia::launcher::Model model(reactor);
    model.open(false);
    model.set_apps({});
    type(model, "xy");
    model.sync_layout();
    test::RecordingCanvas canvas;
    astralia::launcher::Frame frame = astralia::launcher::paint(canvas, model, 1920, 1200);
    test::check(frame.box.w == cfg::width && frame.box.x == (1920 - cfg::width) / 2 &&
                    frame.rows.empty(),
                "the card is centered and has no rows");
    test::check(frame.caret.w == cfg::caret_width, "the caret is reported for input methods");
    test::check(canvas.count(Kind::begin_group) == canvas.count(Kind::end_group) &&
                    canvas.count(Kind::begin_group) >= 2,
                "groups are balanced");
    bool query = false;
    for (const test::Op &op : canvas.ops) {
        query = query || (op.kind == Kind::text && op.text == "xy");
    }
    test::check(query, "the query is drawn");

    auto plain = [](const test::RecordingCanvas &recorded) {
        std::vector<test::Op> ops;
        for (const test::Op &op : recorded.ops) {
            if (op.kind != Kind::opacity) {
                ops.push_back(op);
            }
        }
        return ops;
    };
    astralia::launcher::EntranceState finished{1.0f, 3.0f};
    test::RecordingCanvas animated;
    astralia::launcher::Frame done =
        astralia::launcher::paint(animated, model, 1920, 1200, &finished);
    std::vector<test::Op> expected = plain(canvas);
    std::vector<test::Op> actual = plain(animated);
    bool identical = expected.size() == actual.size() && done.box.y == frame.box.y;
    for (std::size_t i = 0; identical && i < expected.size(); ++i) {
        identical = expected[i].kind == actual[i].kind && expected[i].box.x == actual[i].box.x &&
                    expected[i].box.y == actual[i].box.y && expected[i].text == actual[i].text;
    }
    test::check(identical, "a finished entrance paints the same card as none");

    astralia::launcher::EntranceState tracing{0.2f, 3.0f};
    test::RecordingCanvas start;
    astralia::launcher::Frame early = astralia::launcher::paint(start, model, 1920, 1200, &tracing);
    test::check(early.box.y == frame.box.y && early.entrance.opacity == 0.0f &&
                    !early.entrance.bolts.empty() &&
                    start.count(Kind::rect) == canvas.count(Kind::rect) + 1,
                "the card is hidden in place under a dimmed backdrop while the "
                "outline is traced");
    model.close();
}

KeyEvent chord(KeyKind kind, bool shift = false, bool ctrl = false) {
    KeyEvent event;
    event.kind = kind;
    event.shift = shift;
    event.ctrl = ctrl;
    return event;
}

KeyEvent shortcut(char letter) {
    KeyEvent event;
    event.kind = KeyKind::Text;
    event.ctrl = true;
    event.base_sym = static_cast<unsigned char>(letter);
    event.text = std::string(1, static_cast<char>(letter - 'a' + 1));
    return event;
}

struct FakeClipboard : astralia::Clipboard {
    std::string stored;
    bool defer = false;
    std::function<void(std::string)> waiting;

    void set(std::string text) override { stored = std::move(text); }
    void request(std::function<void(std::string)> done) override {
        if (defer) {
            waiting = std::move(done);
        } else {
            done(stored);
        }
    }
};

std::string selected(const astralia::launcher::Model &model) {
    return astralia::text_cursor_selected_text(model.field().text, model.cursor());
}

void check_caret_editing() {
    Instant instant;
    astralia::PollReactor reactor = make_reactor();
    astralia::launcher::Model model(reactor);
    model.open(false);
    model.set_search_root("/nonexistent-root");
    model.set_apps({});

    type(model, "hello");
    model.key(key(KeyKind::Left));
    model.key(key(KeyKind::Left));
    test::check(model.cursor().pos == 3 && !model.cursor().has_selection(),
                "left moves the caret back");
    type(model, "X");
    test::check(model.field().text == "helXlo" && model.cursor().pos == 4,
                "typing inserts at the caret");
    model.key(key(KeyKind::Backspace));
    test::check(model.field().text == "hello" && model.cursor().pos == 3,
                "backspace removes before the caret");
    model.key(key(KeyKind::Delete));
    test::check(model.field().text == "helo" && model.cursor().pos == 3,
                "delete removes after the caret");
    model.key(key(KeyKind::Home));
    test::check(model.cursor().pos == 0, "home goes to the start");
    model.key(key(KeyKind::Backspace));
    test::check(model.field().text == "helo", "backspace at the start does nothing");
    model.key(key(KeyKind::End));
    test::check(model.cursor().pos == 4, "end goes to the end");
    model.key(key(KeyKind::Delete));
    test::check(model.field().text == "helo", "delete at the end does nothing");
    model.key(key(KeyKind::Right));
    test::check(model.cursor().pos == 4, "right stops at the end");

    model.key(chord(KeyKind::Left, true));
    model.key(chord(KeyKind::Left, true));
    test::check(selected(model) == "lo" && model.cursor().pos == 2,
                "shift and left extend the selection");
    model.key(chord(KeyKind::Right, true));
    test::check(selected(model) == "o", "shift and right shrink the selection");
    model.key(chord(KeyKind::Left, true));
    model.key(key(KeyKind::Left));
    test::check(!model.cursor().has_selection() && model.cursor().pos == 2,
                "left collapses a selection to its start");
    model.key(chord(KeyKind::End, true));
    test::check(selected(model) == "lo", "shift and end select to the end");
    model.key(key(KeyKind::Right));
    test::check(!model.cursor().has_selection() && model.cursor().pos == 4,
                "right collapses a selection to its end");
    model.key(chord(KeyKind::Home, true));
    type(model, "go");
    test::check(model.field().text == "go" && model.cursor().pos == 2,
                "typing replaces the selection");
    model.key(key(KeyKind::Left));
    model.key(chord(KeyKind::Right, true));
    model.key(key(KeyKind::Backspace));
    test::check(model.field().text == "g", "backspace deletes the selection");

    astralia::launcher::Model words(reactor);
    words.open(false);
    words.set_apps({});
    type(words, "foo bar");
    words.key(chord(KeyKind::Left, false, true));
    test::check(words.cursor().pos == 4, "ctrl and left jump to the word start");
    words.key(chord(KeyKind::Left, false, true));
    test::check(words.cursor().pos == 0, "ctrl and left jump again");
    words.key(chord(KeyKind::Right, false, true));
    test::check(words.cursor().pos == 3, "ctrl and right jump to the word end");
    words.key(chord(KeyKind::Right, true, true));
    test::check(selected(words) == " bar" && words.cursor().pos == 7,
                "ctrl, shift and right select by word");

    astralia::launcher::Model utf(reactor);
    utf.open(false);
    utf.set_apps({});
    type(utf, "a");
    utf.key(key(KeyKind::Text, "ñ"));
    type(utf, "b");
    utf.key(key(KeyKind::Left));
    test::check(utf.cursor().pos == 3, "the caret steps over a whole character");
    utf.key(key(KeyKind::Left));
    test::check(utf.cursor().pos == 1, "the caret steps over a two byte character");
    utf.key(key(KeyKind::Delete));
    test::check(utf.field().text == "ab", "delete removes a whole character");
    utf.key(key(KeyKind::Backspace));
    test::check(utf.field().text == "b" && utf.cursor().pos == 0,
                "backspace removes a whole character");
    test::check(utf.query_anim().chars.size() == 1,
                "the typing animation follows edits in the middle");
}

void check_clipboard_keys() {
    Instant instant;
    astralia::PollReactor reactor = make_reactor();
    astralia::launcher::Model model(reactor);
    model.open(false);
    model.set_search_root("/nonexistent-root");
    model.set_apps({});
    FakeClipboard clipboard;
    model.set_clipboard(&clipboard);

    type(model, "hello world");
    model.key(shortcut('c'));
    test::check(clipboard.stored.empty() && model.field().text == "hello world",
                "copy without a selection does nothing and inserts no text");
    model.key(shortcut('a'));
    test::check(selected(model) == "hello world", "ctrl and a select everything");
    model.key(shortcut('c'));
    test::check(clipboard.stored == "hello world" && model.field().text == "hello world",
                "ctrl and c copy the selection");

    model.key(key(KeyKind::Right));
    model.key(shortcut('v'));
    test::check(model.field().text == "hello worldhello world" && model.cursor().pos == 22,
                "ctrl and v paste at the caret");

    model.key(chord(KeyKind::Home, true));
    model.key(shortcut('x'));
    test::check(model.field().text.empty() && clipboard.stored == "hello worldhello world",
                "ctrl and x cut the selection");
    clipboard.stored = "a\nb\tc";
    model.key(shortcut('v'));
    test::check(model.field().text == "a b c", "pasted line breaks become spaces");
    model.key(key(KeyKind::Left));
    clipboard.stored = "";
    model.key(shortcut('v'));
    test::check(model.field().text == "a b c", "pasting nothing changes nothing");

    KeyEvent chord_text = key(KeyKind::Text, "q");
    chord_text.ctrl = true;
    chord_text.base_sym = 'q';
    model.key(chord_text);
    KeyEvent alt_text = key(KeyKind::Text, "q");
    alt_text.alt = true;
    model.key(alt_text);
    test::check(model.field().text == "a b c", "ctrl and alt chords are not typed");

    clipboard.defer = true;
    model.key(shortcut('v'));
    model.close();
    clipboard.waiting("late");
    test::check(model.field().text.empty(), "a paste that arrives after closing is dropped");

    astralia::launcher::Model loose(reactor);
    loose.open(false);
    loose.set_apps({});
    type(loose, "abc");
    loose.key(shortcut('a'));
    loose.key(shortcut('c'));
    loose.key(shortcut('v'));
    test::check(loose.field().text == "abc", "clipboard keys without a clipboard do nothing");
}

void check_pointer_editing() {
    using Clock = astralia::launcher::Model::Clock;
    Instant instant;
    astralia::PollReactor reactor = make_reactor();
    astralia::launcher::Model model(reactor);
    model.open(false);
    model.set_search_root("/nonexistent-root");
    model.set_apps({});
    type(model, "hello world");

    Clock::time_point t0 = Clock::now();
    model.pointer_down(2.2f, t0);
    test::check(model.cursor().pos == 2 && !model.cursor().has_selection(),
                "a click places the caret on the nearest boundary");
    model.pointer_drag(5.4f);
    test::check(selected(model) == "llo", "dragging selects");
    model.pointer_up();
    model.pointer_drag(9.0f);
    test::check(selected(model) == "llo", "moving without the button leaves the selection alone");
    model.pointer_down(40.0f, t0 + std::chrono::seconds(2));
    test::check(model.cursor().pos == 11 && !model.cursor().has_selection(),
                "a click past the text goes to the end");
    model.pointer_up();

    Clock::time_point t1 = t0 + std::chrono::seconds(10);
    model.pointer_down(7.1f, t1);
    model.pointer_up();
    model.pointer_down(7.1f, t1 + std::chrono::milliseconds(100));
    test::check(selected(model) == "world", "a double click selects the word");
    model.pointer_drag(0.0f);
    test::check(selected(model) == "world", "a double click does not start a drag");
    model.pointer_up();

    model.pointer_down(7.1f, t1 + std::chrono::seconds(5));
    model.pointer_up();
    model.pointer_down(7.1f, t1 + std::chrono::seconds(5) + std::chrono::milliseconds(900));
    test::check(!model.cursor().has_selection(), "slow clicks are not a double click");

    test::check(model.field_window(20) == 0, "short text needs no scrolling");
    model.key(key(KeyKind::End));
    test::check(model.field_window(5) == 6, "long text scrolls to keep the caret visible");
    model.key(key(KeyKind::Home));
    test::check(model.field_window(5) == 0, "moving the caret back scrolls back");
    model.key(key(KeyKind::End));
    test::check(model.field_window(5) == 6, "the end scrolls forward again");
    model.key(key(KeyKind::Left));
    test::check(model.field_window(5) == 6, "the window stays while the caret is inside it");
}

void check_field_paint() {
    using Kind = test::Op::Kind;
    Instant instant;
    astralia::PollReactor reactor = make_reactor();
    astralia::launcher::Model model(reactor);
    model.open(false);
    model.set_apps({});
    type(model, "abc");
    model.sync_layout();

    test::RecordingCanvas plain;
    astralia::launcher::Frame frame = astralia::launcher::paint(plain, model, 1920, 1200);
    test::check(frame.field.w > 0.0f && astralia::launcher::field_hit(frame, frame.field.x + 1.0f,
                                                                      frame.field.y + 1.0f),
                "the field is reported for pointer hits");
    test::check(!astralia::launcher::field_hit(frame, frame.field.x - 1.0f, frame.field.y + 1.0f),
                "points outside the field miss");
    test::check(astralia::launcher::field_column(frame, frame.text_x) == 0.0f &&
                    astralia::launcher::field_column(frame, frame.text_x - 50.0f) == 0.0f,
                "columns start at the text origin");
    float caret_at_end = frame.caret.x;

    model.key(key(KeyKind::Left));
    test::RecordingCanvas moved;
    astralia::launcher::Frame left = astralia::launcher::paint(moved, model, 1920, 1200);
    test::check(left.caret.x < caret_at_end, "the caret follows the cursor");
    test::check(moved.count(Kind::rect) == plain.count(Kind::rect),
                "no selection draws no highlight");

    model.key(chord(KeyKind::Left, true));
    test::RecordingCanvas highlighted;
    astralia::launcher::paint(highlighted, model, 1920, 1200);
    test::check(highlighted.count(Kind::rect) == plain.count(Kind::rect) + 1,
                "a selection draws one highlight");
    model.close();
}

} // namespace

void check_launcher_model() {
    check_modes_and_layout();
    check_search_flow();
    check_layout_animation();
    check_view();
    check_caret_editing();
    check_clipboard_keys();
    check_pointer_editing();
    check_field_paint();
}
