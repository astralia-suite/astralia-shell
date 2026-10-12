#include <algorithm>
#include <chrono>

#include "config/osd_config.h"

#include "modules/osd/view.h"
#include "modules/osd/x11/osd.h"

namespace astralia {

namespace {

namespace cfg = osd_config;

} // namespace

Osd::Osd(XConnection &x, EventLoop &loop, Services &services)
    : x_(x), loop_(loop), services_(services) {
    timer_ = loop_.add_timer([this] { return until_hide(); }, [this] { hide(); });
    services_.brightness.changed.connect(
        [this] { show(OsdKind::brightness, services_.brightness.percent(), false); });
    services_.audio.changed.connect([this](AudioKind kind) {
        if (kind == AudioKind::sink) {
            AudioLevel level = services_.audio.sink();
            show(OsdKind::volume, level.present ? level.percent : 0, level.muted);
        } else if (kind == AudioKind::source) {
            AudioLevel level = services_.audio.source();
            show(OsdKind::mic, level.present ? level.percent : 0, level.muted);
        }
    });
}

Osd::Surface &Osd::surface_for(const std::string &output) {
    auto it = surfaces_.find(output);
    if (it == surfaces_.end()) {
        it = surfaces_.emplace(output, std::make_unique<Surface>(x_)).first;
        Surface *surface = it->second.get();
        surface->window.make_click_through();
        loop_.on_window(surface->window.id(), [surface](const xcb_generic_event_t &event) {
            if (surface->window.mapped() && (event.response_type & ~0x80) == XCB_EXPOSE) {
                surface->window.present();
            }
        });
    }
    return *it->second;
}

void Osd::show(OsdKind kind, int percent, bool muted) {
    if (!model_.show(kind, percent, muted, OsdModel::Clock::now())) {
        return;
    }
    bool shown = false;
    for (const Output &output : services_.outputs.outputs()) {
        if (!osd_effective_enabled(services_.settings.config(), output.name)) {
            continue;
        }
        Surface &surface = surface_for(output.name);
        const OutputGeometry &area = output.geometry;
        surface.window.place({static_cast<int16_t>(area.x + (area.width - cfg::width) / 2),
                              static_cast<int16_t>(area.y + area.height - cfg::margin_bottom - cfg::height),
                              cfg::width, cfg::height});
        paint(surface);
        surface.window.show(false);
        shown = true;
    }
    if (shown) {
        loop_.reschedule(timer_);
    }
}

void Osd::hide() {
    if (OsdModel::Clock::now() < model_.hide_at()) {
        return;
    }
    model_.hide();
    for (auto &[name, surface] : surfaces_) {
        surface->window.hide();
    }
}

std::chrono::milliseconds Osd::until_hide() const {
    bool visible = std::ranges::any_of(surfaces_, [](const auto &entry) { return entry.second->window.mapped(); });
    if (!visible) {
        return std::chrono::hours(1);
    }
    return model_.until_hide(OsdModel::Clock::now());
}

void Osd::paint(Surface &surface) {
    surface.window.clear();
    surface.canvas.bind(surface.window.cr());
    paint_osd(surface.canvas, model_);
    surface.window.present();
}

} // namespace astralia
