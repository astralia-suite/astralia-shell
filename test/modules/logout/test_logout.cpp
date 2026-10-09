#include <chrono>
#include <string>
#include <vector>

#include "check.h"
#include "ui/recording_canvas.h"

#include "config/logout_config.h"

#include "core/animation.h"

#include "modules/logout/layout.h"
#include "modules/logout/model.h"
#include "modules/logout/view.h"

namespace {

using astralia::LogoutModel;
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

struct Harness {
    LogoutModel model{astralia::logout_commands(true)};
    std::vector<std::string> executed;
    int closed = 0;

    Harness() {
        model.on_execute = [this](const std::string &command) { executed.push_back(command); };
        model.on_closed = [this] { ++closed; };
    }
};

void check_commands() {
    auto plain = astralia::logout_commands(false);
    auto lock = astralia::logout_commands(true);
    test::check(plain[0] == "systemctl poweroff" && plain[1] == "systemctl reboot", "default commands come from the config");
    test::check(plain[astralia::logout_config::lock_index].empty() && lock[astralia::logout_config::lock_index] == "astralia lock", "the lock command exists only where lock does");
}

void check_logout_model() {
    Instant instant;
    Harness h;
    LogoutModel &m = h.model;
    test::check(!m.open_state() && !m.key(key(KeyKind::Right)), "closed model ignores keys");

    m.open();
    test::check(m.open_state() && m.input_ready() && !m.exiting() && m.logo_scale() == 1.0f && m.burst() == 1.0f, "instant open runs the whole choreography");
    test::check(m.travel()[0] == 1.0f && m.travel()[7] == 1.0f && m.slash()[3] == 1.0f, "buttons sit on the ring");
    test::check(m.selected() == 0 && m.highlight_scale()[0] == 1.0f && m.highlight_border()[0] == 1.0f, "the first button starts selected and highlighted");
    test::check(!m.animating(), "nothing is left animating");

    test::check(m.key(key(KeyKind::Right)) && m.selected() == 1 && m.highlight_scale()[1] == 1.0f && m.highlight_scale()[0] == 0.0f, "right moves the highlight");
    test::check(m.key(key(KeyKind::Left)) && m.key(key(KeyKind::Left)) && m.selected() == 7, "left wraps around");
    test::check(m.key(key(KeyKind::Text, "3")) && m.selected() == 2, "digits select a button");
    test::check(!m.key(key(KeyKind::Text, "9")) && !m.key(key(KeyKind::Text, "ab")) && m.selected() == 2, "other text is ignored");

    test::check(m.hover(0, 0, 1920, 1200) == false, "hover on empty space changes nothing");
    astralia::Point center{960, 600};
    astralia::Point first = astralia::logout_button_center(0, center);
    test::check(m.hover(first.x, first.y, 1920, 1200) && m.hovered() == 0, "hover finds a button");
    test::check(!m.hover(first.x + 1, first.y + 1, 1920, 1200), "same button is not a change");
    test::check(m.clear_hover() && m.hovered() == -1 && !m.clear_hover(), "leaving clears the hover once");

    m.key(key(KeyKind::Text, "1"));
    test::check(m.key(key(KeyKind::Enter)) && h.executed.size() == 1 && h.executed[0] == "systemctl poweroff", "enter runs the selected command");
    test::check(!m.open_state() && h.closed == 1 && m.selected() == 0 && m.travel()[0] == 0.0f, "executing hides the card and resets");

    m.open();
    m.key(key(KeyKind::Text, "3"));
    m.key(key(KeyKind::Enter));
    test::check(h.executed.size() == 2 && h.executed[1] == "astralia lock", "the lock button runs the lock command");

    m.open();
    m.key(key(KeyKind::Text, "5"));
    m.key(key(KeyKind::Enter));
    test::check(h.executed.size() == 2 && !m.open_state() && h.closed == 3, "a button without a command still closes");

    m.open();
    test::check(m.key(key(KeyKind::Escape)) && !m.open_state() && h.closed == 4 && m.exit_fade() == 0.0f, "escape closes through the exit sequence");

    m.open();
    m.click(10, 10, 1920, 1200);
    test::check(!m.open_state() && h.closed == 5, "a click outside closes");
    m.open();
    astralia::Point third = astralia::logout_button_center(1, center);
    m.click(third.x, third.y, 1920, 1200);
    test::check(h.executed.size() == 3 && h.executed[2] == "systemctl reboot" && !m.open_state(), "a click on a button runs it");
}

void check_logout_choreography() {
    Harness h;
    LogoutModel &m = h.model;
    m.open();
    test::check(m.open_state() && !m.input_ready() && m.animating() && m.logo_scale() == 0.0f, "animated open starts from nothing");
    test::check(!m.key(key(KeyKind::Right)), "keys wait for the burst to finish");
    auto start = std::chrono::steady_clock::now();
    m.tick(start + std::chrono::milliseconds(700));
    test::check(m.logo_scale() == 1.0f, "the logo has scaled in");
    for (int ms = 700; ms < 4000 && !m.input_ready(); ms += 50) {
        m.tick(start + std::chrono::milliseconds(ms));
    }
    test::check(m.input_ready(), "input opens once the choreography ends");
    m.toggle();
    test::check(m.open_state() && m.exiting() && !m.input_ready(), "closing plays the exit first");
    for (int ms = 0; ms < 4000 && m.open_state(); ms += 50) {
        m.tick(std::chrono::steady_clock::now() + std::chrono::milliseconds(ms + 5000));
    }
    test::check(!m.open_state() && h.closed == 1, "the card closes when the exit ends");
    m.open();
    m.fast_hide();
    test::check(!m.open_state() && !m.animating() && h.closed == 2, "fast hide stops everything");
}

void check_logout_view() {
    using Kind = test::Op::Kind;
    Harness idle;
    test::RecordingCanvas none;
    astralia::paint_logout(none, idle.model, 1920, 1200, {});
    test::check(none.ops.empty(), "a closed card draws nothing");

    Instant instant;
    Harness h;
    h.model.open();
    test::RecordingCanvas canvas;
    int logo_calls = 0;
    float logo_alpha = 0;
    astralia::paint_logout(canvas, h.model, 1920, 1200, [&](astralia::ui::Canvas &, const astralia::ui::Box &area, float alpha) {
        ++logo_calls;
        logo_alpha = alpha;
        test::check(area.w == astralia::logout_config::logo_size && area.x == 0, "logo area is local to its group");
    });
    test::check(canvas.count(Kind::rounded) == 8 && canvas.count(Kind::text) == 8, "eight buttons with a glyph each");
    test::check(canvas.count(Kind::begin_group) == 1 && canvas.count(Kind::end_group) == 1 && logo_calls == 1 && logo_alpha == 1.0f, "one logo group");
    const test::Op &top = canvas.ops[0];
    test::check(top.box.w > astralia::logout_config::button_size && top.box.w < astralia::logout_config::button_size * 1.06, "the selected button is enlarged");
    const test::Op &next = canvas.ops[2];
    test::check(next.box.w == astralia::logout_config::button_size, "the others keep their size");
    const test::Op &group = canvas.ops.back().kind == Kind::end_group ? canvas.ops[canvas.ops.size() - 2] : canvas.ops.back();
    (void)group;
    bool centered = false;
    for (const test::Op &op : canvas.ops) {
        if (op.kind == Kind::begin_group) {
            centered = op.box.x == 960 - astralia::logout_config::logo_size / 2.0f && op.value == 1.0f;
        }
    }
    test::check(centered, "the logo is centered at full scale");
}

} // namespace

void check_logout() {
    check_commands();
    check_logout_model();
    check_logout_choreography();
    check_logout_view();
}
