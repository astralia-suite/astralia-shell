#include <algorithm>

#include "modules/bar/panel/panel_set.h"

namespace astralia {

void PanelSet::add(PanelId id, std::unique_ptr<PanelContent> content) {
    auto panel = std::make_unique<Panel>(std::move(content), reactor_);
    panel->on_changed = [this] {
        if (on_changed) {
            on_changed();
        }
    };
    panel->on_closed = [this, id] {
        if (latest_ == id) {
            latest_ = PanelId::count;
        }
        if (on_state) {
            on_state(id, false);
        }
        if (on_changed) {
            on_changed();
        }
    };
    panels_[static_cast<size_t>(id)] = std::move(panel);
}

Panel *PanelSet::find(PanelId id) {
    return panels_[static_cast<size_t>(id)].get();
}

const Panel *PanelSet::find(PanelId id) const {
    return panels_[static_cast<size_t>(id)].get();
}

PanelId PanelSet::active_id() const {
    if (latest_ != PanelId::count) {
        const Panel *panel = find(latest_);
        if (panel != nullptr && panel->is_open() && !panel->closing()) {
            return latest_;
        }
    }
    return PanelId::count;
}

Panel *PanelSet::active() {
    PanelId id = active_id();
    return id == PanelId::count ? nullptr : find(id);
}

bool PanelSet::any_open() const {
    return std::ranges::any_of(panels_, [](const auto &p) { return p && p->is_open(); });
}

bool PanelSet::animating() const {
    return std::ranges::any_of(panels_, [](const auto &p) { return p && p->is_open() && p->animating(); });
}

void PanelSet::close_except(PanelId keep) {
    for (size_t i = 0; i < slots; ++i) {
        if (i != static_cast<size_t>(keep) && panels_[i] && panels_[i]->is_open()) {
            panels_[i]->close();
        }
    }
}

void PanelSet::close_all() {
    close_except(PanelId::count);
}

void PanelSet::toggle(PanelId id) {
    Panel *panel = find(id);
    if (panel == nullptr) {
        return;
    }
    if (panel->is_open() && !panel->closing()) {
        panel->close();
        return;
    }
    close_except(id);
    latest_ = id;
    panel->open();
    if (on_state) {
        on_state(id, true);
    }
}

void PanelSet::tick(std::chrono::steady_clock::time_point now) {
    for (auto &panel : panels_) {
        if (panel && panel->is_open()) {
            panel->tick(now);
        }
    }
}

ui::Box PanelSet::paint(ui::Canvas &canvas, float surface_width, float top) {
    ui::Box box;
    for (auto &panel : panels_) {
        if (panel && panel->is_open()) {
            ui::Box card = panel->paint(canvas, surface_width, top);
            if (panel.get() == active()) {
                box = card;
            }
        }
    }
    return box;
}

bool PanelSet::clickable(double x, double y) const {
    return std::ranges::any_of(panels_, [&](const auto &p) { return p && p->clickable(x, y); });
}

bool PanelSet::contains(double x, double y) const {
    return std::ranges::any_of(panels_, [&](const auto &p) { return p && p->contains(x, y); });
}

} // namespace astralia
