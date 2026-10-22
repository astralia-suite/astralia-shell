#include <algorithm>
#include <malloc.h>
#include <xcb/screensaver.h>

#include "config/idle_config.h"

#include "core/log.h"

#include "render/tokens.h"
#include "render/wallpaper_paint.h"

#include "modules/idle/x11/idle.h"

namespace astralia::idle {

namespace {

constexpr std::chrono::milliseconds poll_interval{1000};
constexpr std::chrono::milliseconds frame_interval{33};
constexpr float max_frame_seconds = 0.5f;

} // namespace

Idle::Idle(XConnection &x, EventLoop &loop, Services &services)
    : x_(x), loop_(loop), services_(services), applied_(services.settings.config()) {
    const xcb_query_extension_reply_t *extension =
        xcb_get_extension_data(x_.conn(), &xcb_screensaver_id);
    available_ = extension != nullptr && extension->present;
    if (!available_) {
        log::error("idle: the X server has no MIT-SCREEN-SAVER extension, idle "
                   "management is off");
    }
    std::vector<std::string> names;
    for (const Output &output : services_.outputs.outputs()) {
        names.push_back(output.name);
    }
    clock_.reset(names, std::chrono::steady_clock::now());
    timer_ = loop_.add_timer([this] { return until_step(); }, [this] { step(); });
    services_.settings.changed.connect([this] {
        const Config &config = services_.settings.config();
        if (policy_changed(applied_, config)) {
            std::vector<std::string> outputs;
            for (const Output &output : services_.outputs.outputs()) {
                outputs.push_back(output.name);
            }
            clock_.reset(outputs, std::chrono::steady_clock::now());
        }
        applied_ = config;
        for (const Output &output : services_.outputs.outputs()) {
            if (auto it = overlays_.find(output.name);
                it != overlays_.end() && it->second->shown != Verdict{}) {
                paint(output, *it->second);
            }
        }
        step();
    });
    services_.outputs.changed.connect([this] {
        hide_missing();
        step();
    });
}

std::chrono::milliseconds Idle::until_step() const {
    return animating() ? frame_interval : poll_interval;
}

bool Idle::animating() const {
    return std::ranges::any_of(overlays_,
                               [](const auto &entry) { return entry.second->shown.screensaver; });
}

Idle::Overlay &Idle::overlay_for(const Output &output) {
    auto it = overlays_.find(output.name);
    if (it == overlays_.end()) {
        it = overlays_.emplace(output.name, std::make_unique<Overlay>(x_)).first;
        it->second->window.make_click_through();
        Overlay *overlay = it->second.get();
        std::string name = output.name;
        loop_.on_window(
            overlay->window.id(), [this, overlay, name](const xcb_generic_event_t &event) {
                if (overlay->window.mapped() && (event.response_type & ~0x80) == XCB_EXPOSE) {
                    overlay->window.present();
                }
            });
    }
    return *it->second;
}

void Idle::poll() {
    auto now = std::chrono::steady_clock::now();
    last_poll_ = now;
    xcb_connection_t *conn = x_.conn();
    xcb_screensaver_query_info_reply_t *reply = xcb_screensaver_query_info_reply(
        conn, xcb_screensaver_query_info(conn, x_.root()), nullptr);
    if (reply == nullptr) {
        return;
    }
    uint32_t idle_ms = reply->ms_since_user_input;
    std::free(reply);
    if (idle_ms >= idle_config::kRecentActivityPulseSeconds * 1000u) {
        return;
    }
    const std::string &focused = services_.compositor->state().focused_monitor;
    if (!focused.empty()) {
        clock_.activity(focused, now);
    }
    clock_.activity(services_.outputs.at_pointer().name, now);
}

void Idle::step() {
    if (!available_) {
        return;
    }
    auto now = std::chrono::steady_clock::now();
    if (now - last_poll_ >= poll_interval) {
        poll();
    }
    float seconds = std::chrono::duration<float>(now - last_frame_).count();
    last_frame_ = now;
    const Config &config = services_.settings.config();
    for (const Output &output : services_.outputs.outputs()) {
        if (!clock_.known(output.name)) {
            clock_.activity(output.name, now);
        }
        Verdict verdict = idle_verdict(clock_, config, output.name, now);
        auto it = overlays_.find(output.name);
        if (it == overlays_.end() && !verdict.ambient && !verdict.screensaver) {
            continue;
        }
        Overlay &overlay = overlay_for(output);
        apply(output, overlay, verdict);
        if (overlay.shown.screensaver && seconds > 0.0f && seconds <= max_frame_seconds) {
            bounce_step(overlay.bounce, seconds, output.geometry.width, output.geometry.height,
                        idle_config::kLogoSize);
            paint(output, overlay);
        }
    }
}

void Idle::apply(const Output &output, Overlay &overlay, Verdict verdict) {
    Verdict next{verdict.ambient && !verdict.screensaver, verdict.screensaver};
    if (next == overlay.shown) {
        return;
    }
    overlay.shown = next;
    if (!next.ambient && !next.screensaver) {
        overlay.window.hide();
        overlay.window.release();
        overlay.canvas.release_images();
        malloc_trim(0);
        return;
    }
    overlay.window.place(output.geometry);
    if (next.screensaver) {
        overlay.bounce = {40.0f, 40.0f, idle_config::kLogoSpeed, idle_config::kLogoSpeed * 0.75f};
    }
    paint(output, overlay);
    overlay.window.show(false);
}

void Idle::hide_missing() {
    for (auto &[name, overlay] : overlays_) {
        const std::vector<Output> &outputs = services_.outputs.outputs();
        if (std::ranges::any_of(outputs,
                                [&name](const Output &output) { return output.name == name; })) {
            continue;
        }
        overlay->shown = {};
        overlay->window.hide();
        overlay->window.release();
        overlay->canvas.release_images();
    }
}

void Idle::paint(const Output &output, Overlay &overlay) {
    if (overlay.window.cr() == nullptr) {
        return;
    }
    cairo_t *cr = overlay.window.cr();
    int width = overlay.window.geometry().width;
    int height = overlay.window.geometry().height;
    overlay.window.clear();
    if (overlay.shown.screensaver) {
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_paint(cr);
        overlay.canvas.bind(cr);
        const std::string &logo_path = services_.settings.config().idle_logo_path;
        ui::ImageId logo =
            logo_path.empty()
                ? ui::no_image
                : overlay.canvas.image(logo_path, static_cast<int>(idle_config::kLogoSize));
        if (logo != ui::no_image) {
            ui::TextSize size = overlay.canvas.image_size(logo);
            overlay.canvas.draw_image(logo, {overlay.bounce.x, overlay.bounce.y, size.w, size.h},
                                      palette::white);
        }
    } else {
        paint_wallpaper(cr, services_.settings.config(), output.name, 0, 0, width, height);
    }
    overlay.window.present();
}

} // namespace astralia::idle
