#include <algorithm>

#include "config/notification_config.h"

#include "modules/notification/model.h"

namespace astralia::notification {

namespace {

namespace cfg = notification_config;

uint64_t owner_opacity(uint32_t id) { return (static_cast<uint64_t>(id) << 2) | 0; }
uint64_t owner_slide(uint32_t id) { return (static_cast<uint64_t>(id) << 2) | 1; }
uint64_t owner_exit(uint32_t id) { return (static_cast<uint64_t>(id) << 2) | 2; }
uint64_t owner_progress(uint32_t id) { return (static_cast<uint64_t>(id) << 2) | 3; }

void load(Entry &entry, const Notification &notification) {
    entry.app = notification.app;
    entry.summary = notification.summary;
    entry.body = notification.body;
    entry.urgency = notification.urgency;
    entry.progress = 1.0f;
    entry.timed = !animation_instant();
}

bool same_content(const Entry &entry, const Notification &notification) {
    return entry.app == notification.app && entry.summary == notification.summary &&
           entry.body == notification.body;
}

} // namespace

Entry *Model::find(uint32_t id) {
    auto it = std::ranges::find(entries_, id, &Entry::id);
    return it == entries_.end() ? nullptr : &*it;
}

void Model::start_progress(uint32_t id, std::chrono::milliseconds timeout) {
    animations_.animate(
        1.0f, 0.0f, static_cast<float>(timeout.count()), astralia::Easing::Linear,
        [this, id](float v) {
            if (Entry *entry = find(id)) {
                entry->progress = v;
            }
        },
        {}, owner_progress(id));
}

void Model::add(const Notification &notification) {
    uint32_t id = notification.id;
    Entry entry;
    entry.id = id;
    load(entry, notification);
    entry.slide_offset = cfg::slide_offset;
    entries_.insert(entries_.begin(), std::move(entry));

    animations_.animate(
        0.0f, 1.0f, cfg::anim_normal_ms, astralia::Easing::EaseOutCubic,
        [this, id](float v) {
            if (Entry *e = find(id)) {
                e->opacity = v;
            }
        },
        {}, owner_opacity(id));
    animations_.animate(
        cfg::slide_offset, 0.0f, cfg::anim_normal_ms, astralia::Easing::EaseOutCubic,
        [this, id](float v) {
            if (Entry *e = find(id)) {
                e->slide_offset = v;
            }
        },
        {}, owner_slide(id));
    start_progress(id, notification.timeout);
}

void Model::start_exit(uint32_t id) {
    Entry *entry = find(id);
    if (entry == nullptr || entry->exiting) {
        return;
    }
    entry->exiting = true;
    animations_.cancel_owner(owner_progress(id));
    animations_.animate(
        entry->opacity, 0.0f, cfg::anim_normal_ms, astralia::Easing::EaseOutCubic,
        [this, id](float v) {
            if (Entry *e = find(id)) {
                e->opacity = v;
            }
        },
        {}, owner_opacity(id));
    animations_.animate(
        0.0f, 1.0f, cfg::anim_normal_ms + cfg::anim_exit_buffer_ms, astralia::Easing::Linear,
        [](float) {},
        [this, id] { std::erase_if(entries_, [id](const Entry &e) { return e.id == id; }); },
        owner_exit(id));
}

void Model::sync(const std::vector<Notification> &list) {
    for (const Notification &notification : list) {
        Entry *entry = find(notification.id);
        if (entry == nullptr) {
            add(notification);
        } else if (!same_content(*entry, notification)) {
            load(*entry, notification);
            start_progress(notification.id, notification.timeout);
        }
    }
    std::vector<uint32_t> gone;
    for (const Entry &entry : entries_) {
        if (entry.exiting) {
            continue;
        }
        bool present =
            std::ranges::any_of(list, [&entry](const Notification &n) { return n.id == entry.id; });
        if (!present) {
            gone.push_back(entry.id);
        }
    }
    for (uint32_t id : gone) {
        start_exit(id);
    }
}

bool ViewState::hide_locally(uint32_t id) {
    if (local_exit_.contains(id)) {
        return false;
    }
    local_exit_[id] = 1.0f;
    animations_.animate(
        1.0f, 0.0f, cfg::anim_normal_ms, astralia::Easing::EaseOutCubic,
        [this, id](float v) {
            if (auto it = local_exit_.find(id); it != local_exit_.end()) {
                it->second = v;
            }
        },
        [this, id] {
            if (auto it = local_exit_.find(id); it != local_exit_.end()) {
                it->second = 0.0f;
            }
        },
        id);
    return true;
}

bool ViewState::set_hover(std::optional<uint32_t> id) {
    uint32_t next = id.value_or(0);
    if (next == hovered_) {
        return false;
    }
    hovered_ = next;
    return true;
}

void ViewState::forget_missing(const std::vector<Entry> &entries) {
    std::erase_if(local_exit_, [&entries](const auto &kv) {
        return std::ranges::none_of(entries, [&kv](const Entry &e) { return e.id == kv.first; });
    });
}

float ViewState::local_opacity(uint32_t id) const {
    auto it = local_exit_.find(id);
    return it == local_exit_.end() ? 1.0f : it->second;
}

float card_height(float header, float summary, float body) {
    float height = header + cfg::card_pad * 2.0f + cfg::extra_height;
    if (summary > 0.0f) {
        height += cfg::content_spacing + summary;
    }
    if (body > 0.0f) {
        height += cfg::content_spacing + body;
    }
    return height;
}

Layout layout_notifications(ui::Canvas &canvas, const Model &model, const ViewState &view,
                            float max_height) {
    ui::TextStyle app_style{ui::FontFamily::text, cfg::app_px, true,
                            static_cast<int>(cfg::wrap_width)};
    ui::TextStyle summary_style{ui::FontFamily::text, cfg::summary_px, true,
                                static_cast<int>(cfg::wrap_width), true};
    ui::TextStyle body_style{ui::FontFamily::text, cfg::body_px, false,
                             static_cast<int>(cfg::wrap_width), true};

    Layout layout;
    float used = 0.0f;
    for (const Entry &entry : model.entries()) {
        if (view.fading(entry.id) && view.local_opacity(entry.id) <= 0.0f) {
            continue;
        }
        Card card;
        card.entry = &entry;
        card.header = std::max(
            cfg::urgency_dot,
            canvas.measure(entry.app.empty() ? cfg::app_fallback : entry.app, app_style).h);
        card.summary =
            entry.summary.empty() ? 0.0f : canvas.measure(entry.summary, summary_style).h;
        card.body = entry.body.empty() ? 0.0f : canvas.measure(entry.body, body_style).h;
        card.height = card_height(card.header, card.summary, card.body);
        float next = used + (layout.cards.empty() ? 0.0f : cfg::gap) + card.height;
        if (!layout.cards.empty() && next > max_height) {
            break;
        }
        used = next;
        layout.cards.push_back(card);
    }
    layout.height = used;
    float y = used;
    for (Card &card : layout.cards) {
        y -= card.height;
        card.y = y;
        y -= cfg::gap;
    }
    return layout;
}

std::vector<std::pair<uint32_t, ui::Box>> close_boxes(const Layout &layout, const ViewState &view) {
    std::vector<std::pair<uint32_t, ui::Box>> boxes;
    for (const Card &card : layout.cards) {
        if (view.fading(card.entry->id)) {
            continue;
        }
        boxes.push_back({card.entry->id,
                         {cfg::card_width - cfg::close_hit, card.y + card.entry->slide_offset,
                          cfg::close_hit, cfg::close_hit}});
    }
    return boxes;
}

std::optional<uint32_t> close_at(const Layout &layout, const ViewState &view, double x, double y) {
    for (const auto &[id, box] : notification::close_boxes(layout, view)) {
        if (x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h) {
            return id;
        }
    }
    return std::nullopt;
}

} // namespace astralia::notification
