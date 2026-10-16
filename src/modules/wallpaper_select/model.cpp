#include <algorithm>
#include <filesystem>
#include <thread>

#include "config/wallpaper_select_config.h"

#include "core/deferred_call.h"

#include "modules/wallpaper_select/model.h"

#include "service/wallpaper_service.h"

namespace astralia {

namespace {

namespace cfg = wallpaper_select_config;

constexpr uint64_t owner_zoom = 1;
constexpr uint64_t owner_step = 2;

using Clock = std::chrono::steady_clock;

Clock::duration millis(float ms) {
    return std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<float, std::milli>(ms));
}

bool same_file(const std::string &a, const std::string &b) {
    return std::filesystem::path(a).lexically_normal() ==
           std::filesystem::path(b).lexically_normal();
}

} // namespace

WallpaperSelectModel::WallpaperSelectModel(WallpaperSelectHooks hooks, bool animated_wallpaper)
    : hooks_(std::move(hooks)), animated_wallpaper_(animated_wallpaper) {}

WallpaperSelectModel::~WallpaperSelectModel() { *alive_ = false; }

bool WallpaperSelectModel::available() const { return true; }

void WallpaperSelectModel::open(const std::string &monitor, int column) {
    if (open_) {
        return;
    }
    const Config &config = hooks_.config();
    monitor_ = monitor;
    column_ = column;
    animated_ = animated_wallpaper_ && config.wallpaper_animated_enabled;
    current_ = animated_ ? wallpaper_column_override(config, monitor, column, true)
                         : wallpaper_column_path(config, monitor, column, false);
    directory_ = animated_ ? config.wallpaper_animated_dir : config.wallpaper_dir;
    files_.clear();
    if (!current_.empty()) {
        files_.push_back(current_);
    }
    selected_ = 0;
    position_ = 0.0f;
    wheel_ = 0.0f;
    zoom_ = 0.0f;
    open_ = true;
    closing_ = false;
    holding_ = false;
    applying_ = false;
    target_.clear();
    seen_.clear();
    waiting_ = true;
    opened_at_ = Clock::now();
    if (current_.empty()) {
        reveal();
    }
    scan();
    changed();
}

void WallpaperSelectModel::start_zoom() {
    animations_.animate(
        0.0f, 1.0f, cfg::zoom_ms, Easing::EaseOutCubic, [this](float v) { zoom_ = v; }, {},
        owner_zoom);
}

void WallpaperSelectModel::reveal() {
    if (!waiting_ || !open_ || closing_) {
        return;
    }
    waiting_ = false;
    seen_[current_] = Clock::time_point{};
    start_zoom();
    if (on_revealed) {
        on_revealed();
    }
    changed();
}

void WallpaperSelectModel::reveal_if_overdue(Clock::time_point now) {
    if (waiting_ && now - opened_at_ >= millis(cfg::reveal_timeout_ms)) {
        reveal();
    }
}

void WallpaperSelectModel::tick(Clock::time_point now) {
    animations_.tick(now);
    if (holding_ && (now >= hold_deadline_ ||
                     (settled_ && now >= settled_at_ + millis(cfg::handoff_settle_ms)))) {
        finish_close();
    }
}

void WallpaperSelectModel::wallpaper_settled(const std::string &monitor, int column) {
    if (!applying_ || settled_ || monitor != monitor_ || column != column_) {
        return;
    }
    settled_ = true;
    settled_at_ = Clock::now();
    changed();
}

float WallpaperSelectModel::fade(const std::string &path, bool ready) {
    if (animation_instant()) {
        return 1.0f;
    }
    if (!ready) {
        return 0.0f;
    }
    auto [at, inserted] = seen_.try_emplace(path, Clock::now());
    float t = std::chrono::duration<float, std::milli>(Clock::now() - at->second).count() /
              cfg::fade_in_ms;
    if (t >= 1.0f) {
        return 1.0f;
    }
    fading_ = true;
    return t;
}

void WallpaperSelectModel::close() {
    if (!open_ || closing_) {
        return;
    }
    closing_ = true;
    waiting_ = false;
    ++generation_;
    animations_.cancelForOwner(owner_step);
    animations_.animate(
        zoom_, 0.0f, cfg::zoom_ms, Easing::EaseInCubic, [this](float v) { zoom_ = v; },
        [this] {
            if (applying_ && !settled_) {
                holding_ = true;
                hold_deadline_ = Clock::now() + millis(cfg::handoff_timeout_ms);
                changed();
                return;
            }
            finish_close();
        },
        owner_zoom);
    changed();
}

void WallpaperSelectModel::finish_close() {
    open_ = false;
    closing_ = false;
    scanning_ = false;
    holding_ = false;
    applying_ = false;
    waiting_ = false;
    files_.clear();
    cards_.clear();
    seen_.clear();
    if (on_closed) {
        on_closed();
    }
}

void WallpaperSelectModel::scan() {
    scanning_ = true;
    uint64_t generation = ++generation_;
    std::weak_ptr<bool> alive = alive_;
    std::string directory = directory_;
    bool animated = animated_;
    std::thread([this, alive, generation, directory, animated] {
        std::vector<std::string> found = wallpaper_list_files(directory, animated, cfg::max_images);
        DeferredCall::call_later([this, alive, generation, found = std::move(found)]() mutable {
            if (!alive.lock() || generation != generation_) {
                return;
            }
            auto at = std::ranges::find_if(
                found, [this](const std::string &path) { return same_file(path, current_); });
            if (at == found.end() && !current_.empty()) {
                found.insert(found.begin(), current_);
                at = found.begin();
            }
            files_ = std::move(found);
            selected_ = at == files_.end() ? 0 : static_cast<int>(at - files_.begin());
            position_ = static_cast<float>(selected_);
            animations_.cancelForOwner(owner_step);
            scanning_ = false;
            changed();
        });
    }).detach();
}

void WallpaperSelectModel::select(int index) {
    if (!open_ || closing_ || files_.empty()) {
        return;
    }
    int target = std::clamp(index, 0, static_cast<int>(files_.size()) - 1);
    if (target == selected_) {
        return;
    }
    selected_ = target;
    animations_.animate(
        position_, static_cast<float>(selected_), cfg::step_ms, Easing::EaseOutCubic,
        [this](float v) {
            position_ = v;
            changed();
        },
        {}, owner_step);
    changed();
}

void WallpaperSelectModel::step(int delta) { select(selected_ + delta); }

void WallpaperSelectModel::scroll(float notches) {
    if (!open_ || closing_) {
        return;
    }
    wheel_ += notches;
    while (wheel_ >= cfg::wheel_notch) {
        wheel_ -= cfg::wheel_notch;
        step(1);
    }
    while (wheel_ <= -cfg::wheel_notch) {
        wheel_ += cfg::wheel_notch;
        step(-1);
    }
}

void WallpaperSelectModel::apply() {
    if (!open_ || closing_ || files_.empty()) {
        return;
    }
    const std::string path = files_[static_cast<size_t>(selected_)];
    if (!same_file(path, current_)) {
        target_ = path;
        applying_ = true;
        hooks_.update(
            [monitor = monitor_, column = column_, animated = animated_, path](Config &config) {
                wallpaper_set_column(config, monitor, column, path, animated);
            });
        settled_ = animated_ || !hooks_.settled || hooks_.settled();
    }
    request_close();
}

void WallpaperSelectModel::request_close() {
    if (on_close_requested) {
        on_close_requested();
    } else {
        close();
    }
}

const WallpaperCard *WallpaperSelectModel::card_at(double x, double y) const {
    for (auto it = cards_.rbegin(); it != cards_.rend(); ++it) {
        const ui::Box &box = it->box;
        if (x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h) {
            return &*it;
        }
    }
    return nullptr;
}

bool WallpaperSelectModel::clickable(double x, double y) const {
    return open_ && !closing_ && card_at(x, y) != nullptr;
}

void WallpaperSelectModel::click(double x, double y) {
    if (!open_ || closing_) {
        return;
    }
    const WallpaperCard *card = card_at(x, y);
    if (card == nullptr) {
        request_close();
    } else if (card->index == selected_) {
        apply();
    } else {
        select(card->index);
    }
}

bool WallpaperSelectModel::key(const input::KeyEvent &event) {
    if (!open_ || closing_) {
        return false;
    }
    switch (event.kind) {
    case input::KeyKind::Left:
        step(-1);
        return true;
    case input::KeyKind::Right:
        step(1);
        return true;
    case input::KeyKind::Home:
        select(0);
        return true;
    case input::KeyKind::End:
        select(static_cast<int>(files_.size()) - 1);
        return true;
    case input::KeyKind::Enter:
        apply();
        return true;
    case input::KeyKind::Escape:
        request_close();
        return true;
    default:
        return false;
    }
}

void WallpaperSelectModel::changed() {
    if (on_changed) {
        on_changed();
    }
}

} // namespace astralia
