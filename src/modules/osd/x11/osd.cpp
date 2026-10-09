#include <algorithm>
#include <chrono>
#include <xcb/shape.h>

#include "config/osd_config.h"

#include "modules/osd/view.h"
#include "modules/osd/x11/osd.h"

namespace astralia {

namespace {

namespace cfg = osd_config;

} // namespace

Osd::Osd(XConnection &x, EventLoop &loop, Services &services)
    : x_(x), loop_(loop), services_(services), window_(x, "astralia-osd", XCB_EVENT_MASK_EXPOSURE) {
    xcb_shape_rectangles(x_.conn(), XCB_SHAPE_SO_SET, XCB_SHAPE_SK_INPUT, XCB_CLIP_ORDERING_UNSORTED,
                         window_.id(), 0, 0, 0, nullptr);
    loop_.on_window(window_.id(), [this](const xcb_generic_event_t &event) {
        if (window_.mapped() && (event.response_type & ~0x80) == XCB_EXPOSE) {
            window_.present();
        }
    });
    timer_ = loop_.add_timer([this] { return until_hide(); }, [this] { hide(); });
    services_.brightness.changed.connect(
        [this] { show(OsdKind::brightness, services_.brightness.percent(), false); });
    services_.audio.changed.connect([this](AudioKind kind) {
        if (kind == AudioKind::nodes) {
            return;
        }
        AudioLevel level =
            kind == AudioKind::sink ? services_.audio.sink() : services_.audio.source();
        if (level.present) {
            show(kind == AudioKind::sink ? OsdKind::volume : OsdKind::mic, level.percent, level.muted);
        }
    });
}

void Osd::show(OsdKind kind, int percent, bool muted) {
    const Output &target = services_.outputs.at_pointer();
    if (!osd_effective_enabled(services_.settings.config(), target.name)) {
        return;
    }
    if (!model_.show(kind, percent, muted, OsdModel::Clock::now())) {
        return;
    }
    const OutputGeometry &output = target.geometry;
    window_.place({static_cast<int16_t>(output.x + (output.width - cfg::width) / 2),
                   static_cast<int16_t>(output.y + output.height - cfg::margin_bottom - cfg::height),
                   cfg::width, cfg::height});
    paint();
    window_.show(false);
    loop_.reschedule(timer_);
}

void Osd::hide() {
    if (!window_.mapped() || OsdModel::Clock::now() < model_.hide_at()) {
        return;
    }
    model_.hide();
    window_.hide();
}

std::chrono::milliseconds Osd::until_hide() const {
    if (!window_.mapped()) {
        return std::chrono::hours(1);
    }
    return model_.until_hide(OsdModel::Clock::now());
}

void Osd::paint() {
    window_.clear();
    canvas_.bind(window_.cr());
    paint_osd(canvas_, model_);
    window_.present();
}

} // namespace astralia
