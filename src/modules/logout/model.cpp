#include <algorithm>

#include "modules/logout/layout.h"
#include "modules/logout/model.h"

namespace astralia {

namespace {

namespace cfg = logout_config;

constexpr uint64_t owner_logo = 1;
constexpr uint64_t owner_close_chain = 3;
constexpr uint64_t owner_exit = 4;
constexpr uint64_t owner_burst = 5;
constexpr uint64_t owner_hold = 6;

uint64_t owner_scale(int i) { return 30 + static_cast<uint64_t>(i); }
uint64_t owner_border(int i) { return 40 + static_cast<uint64_t>(i); }
uint64_t owner_slash(int i) { return 50 + static_cast<uint64_t>(i); }
uint64_t owner_push(int i) { return 60 + static_cast<uint64_t>(i); }
uint64_t owner_gate(int i) { return 80 + static_cast<uint64_t>(i); }

} // namespace

std::array<std::string, logout_config::button_count> logout_commands(bool with_lock) {
    std::array<std::string, logout_config::button_count> commands;
    for (size_t i = 0; i < commands.size(); ++i) {
        commands[i] = cfg::actions[i].command;
    }
    if (with_lock) {
        commands[static_cast<size_t>(cfg::lock_index)] = cfg::lock_command;
    }
    return commands;
}

LogoutModel::LogoutModel(Commands commands) : commands_(std::move(commands)) {}

void LogoutModel::schedule_after(float delay_ms, uint64_t owner, std::function<void()> fn) {
    animations_.animate(0.0f, 1.0f, delay_ms, astralia::Easing::Linear, [](float) {}, std::move(fn), owner);
}

void LogoutModel::set_highlight(int index, bool on) {
    if (index < 0 || index >= count) {
        return;
    }
    auto i = static_cast<size_t>(index);
    float target = on ? 1.0f : 0.0f;
    animations_.animate(highlight_scale_[i], target, cfg::button_scale_ms, astralia::Easing::EaseOutCubic, [this, i](float v) { highlight_scale_[i] = v; }, {}, owner_scale(index));
    animations_.animate(highlight_border_[i], target, cfg::button_border_ms, astralia::Easing::EaseOutCubic, [this, i](float v) { highlight_border_[i] = v; }, {}, owner_border(index));
}

void LogoutModel::update_highlight(int index) {
    set_highlight(index, index == selected_ || index == hovered_);
}

void LogoutModel::select(int index) {
    int old = selected_;
    selected_ = index;
    update_highlight(old);
    update_highlight(selected_);
}

void LogoutModel::cancel_sequence() {
    animations_.cancelForOwner(owner_logo);
    animations_.cancelForOwner(owner_close_chain);
    animations_.cancelForOwner(owner_exit);
    animations_.cancelForOwner(owner_burst);
    animations_.cancelForOwner(owner_hold);
    for (int i = 0; i < count; ++i) {
        animations_.cancelForOwner(owner_slash(i));
        animations_.cancelForOwner(owner_push(i));
        animations_.cancelForOwner(owner_gate(i));
    }
}

void LogoutModel::finish_close() {
    open_ = false;
    if (on_closed) {
        on_closed();
    }
}

void LogoutModel::start_burst() {
    burst_ = 0.0f;
    animations_.animate(0.0f, 1.0f, cfg::burst_ms, astralia::Easing::EaseOutCubic, [this](float v) { burst_ = v; }, [this] {
        input_ready_ = true;
        update_highlight(selected_); }, owner_burst);
    for (int i = 0; i < count; ++i) {
        auto idx = static_cast<size_t>(i);
        animations_.animate(0.0f, 1.0f, cfg::push_ms, astralia::Easing::EaseOutBack, [this, idx](float v) { travel_[idx] = v; }, {}, owner_push(i));
    }
}

void LogoutModel::start_exit_burst() {
    burst_ = 0.0f;
    exit_fade_ = 1.0f;
    animations_.animate(0.0f, 1.0f, cfg::burst_ms, astralia::Easing::EaseOutCubic, [this](float v) { burst_ = v; }, [this] { finish_close(); }, owner_burst);
    animations_.animate(1.0f, 0.0f, cfg::exit_fade_ms, astralia::Easing::EaseOutCubic, [this](float v) { exit_fade_ = v; }, {}, owner_exit);
    for (int i = 0; i < count; ++i) {
        auto idx = static_cast<size_t>(i);
        animations_.animate(travel_[idx], 1.0f + cfg::exit_spread, cfg::exit_fade_ms, astralia::Easing::EaseOutCubic, [this, idx](float v) { travel_[idx] = v; }, {}, owner_push(i));
    }
}

void LogoutModel::start_slashes() {
    float step = cfg::slash_ms * cfg::slash_advance_frac;
    for (int e = 0; e < count; ++e) {
        auto idx = static_cast<size_t>(e);
        schedule_after(static_cast<float>(e) * step, owner_gate(e), [this, idx, e] {
            animations_.animate(0.0f, 1.0f, cfg::slash_ms, astralia::Easing::EaseOutCubic, [this, idx](float v) { slash_[idx] = v; }, {}, owner_slash(e));
        });
    }
    float slash_time = static_cast<float>(count - 1) * step + cfg::slash_ms;
    schedule_after(slash_time + cfg::hold_ms, owner_hold, [this] {
        if (exiting_) {
            start_exit_burst();
        } else {
            start_burst();
        }
    });
}

void LogoutModel::start_open() {
    cancel_sequence();
    exiting_ = false;
    input_ready_ = false;
    logo_scale_ = 0.0f;
    exit_fade_ = 0.0f;
    burst_ = 0.0f;
    burst_started_ = std::chrono::steady_clock::now();
    slash_.fill(0.0f);
    travel_.fill(0.0f);
    animations_.animate(0.0f, 1.0f, cfg::logo_anim_ms, astralia::Easing::EaseOutBack, [this](float v) { logo_scale_ = v; }, [this] { schedule_after(cfg::hold_ms, owner_hold, [this] { start_slashes(); }); }, owner_logo);
}

void LogoutModel::start_close() {
    cancel_sequence();
    input_ready_ = false;
    exiting_ = true;
    burst_ = 0.0f;
    exit_fade_ = 1.0f;
    burst_started_ = std::chrono::steady_clock::now();
    int previous = hovered_;
    hovered_ = -1;
    set_highlight(selected_, false);
    set_highlight(previous, false);
    slash_.fill(0.0f);
    start_slashes();
}

void LogoutModel::fast_hide() {
    cancel_sequence();
    for (int i = 0; i < count; ++i) {
        auto idx = static_cast<size_t>(i);
        slash_[idx] = 0.0f;
        travel_[idx] = 0.0f;
        highlight_scale_[idx] = 0.0f;
        highlight_border_[idx] = 0.0f;
        animations_.cancelForOwner(owner_scale(i));
        animations_.cancelForOwner(owner_border(i));
    }
    hovered_ = -1;
    logo_scale_ = 0.0f;
    exit_fade_ = 0.0f;
    burst_ = 0.0f;
    exiting_ = false;
    input_ready_ = false;
    finish_close();
}

void LogoutModel::open() {
    selected_ = 0;
    open_ = true;
    start_open();
}

void LogoutModel::toggle() {
    if (open_) {
        start_close();
    } else {
        open();
    }
}

void LogoutModel::execute(int index) {
    if (index < 0 || index >= count) {
        return;
    }
    const std::string command = commands_[static_cast<size_t>(index)];
    if (!command.empty() && on_execute) {
        on_execute(command);
    }
    selected_ = 0;
    fast_hide();
}

bool LogoutModel::key(const input::KeyEvent &event) {
    if (!input_ready_) {
        return false;
    }
    switch (event.kind) {
    case input::KeyKind::Left:
        select((selected_ + count - 1) % count);
        return true;
    case input::KeyKind::Right:
        select((selected_ + 1) % count);
        return true;
    case input::KeyKind::Text:
        if (event.text.size() == 1 && event.text[0] >= '1' && event.text[0] < '1' + count) {
            select(event.text[0] - '1');
            return true;
        }
        return false;
    case input::KeyKind::Enter:
        execute(selected_);
        return true;
    case input::KeyKind::Escape:
        toggle();
        return true;
    default:
        return false;
    }
}

void LogoutModel::click(double x, double y, double width, double height) {
    if (auto button = logout_button_at({x, y}, {width / 2.0, height / 2.0})) {
        if (input_ready_) {
            execute(*button);
        }
        return;
    }
    toggle();
}

bool LogoutModel::hover(double x, double y, double width, double height) {
    if (!input_ready_) {
        return false;
    }
    int found = logout_button_at({x, y}, {width / 2.0, height / 2.0}).value_or(-1);
    if (found == hovered_) {
        return false;
    }
    int old = hovered_;
    hovered_ = found;
    update_highlight(old);
    update_highlight(found);
    return true;
}

bool LogoutModel::clear_hover() {
    if (hovered_ == -1) {
        return false;
    }
    int old = hovered_;
    hovered_ = -1;
    update_highlight(old);
    return true;
}

void LogoutModel::tick(std::chrono::steady_clock::time_point now) {
    animations_.tick(now);
}

} // namespace astralia
