#include <algorithm>
#include <chrono>
#include <malloc.h>
#include <numbers>

#include "config/visualizer_config.h"

#include "core/pointer.h"

#include "modules/visualizer/bars.h"
#include "modules/visualizer/fft.h"
#include "modules/visualizer/x11/visualizer.h"

#include "render/tokens.h"

namespace astralia {

namespace {

constexpr const char *title = "Visualizer";
constexpr std::chrono::milliseconds idle_interval{std::chrono::hours(1)};

void top_rounded(cairo_t *cr, double x, double y, double width, double height, double radius) {
    cairo_new_sub_path(cr);
    cairo_move_to(cr, x, y + height);
    cairo_line_to(cr, x, y + radius);
    cairo_arc(cr, x + radius, y + radius, radius, std::numbers::pi, 1.5 * std::numbers::pi);
    cairo_arc(cr, x + width - radius, y + radius, radius, 1.5 * std::numbers::pi, 0.0);
    cairo_line_to(cr, x + width, y + height);
    cairo_close_path(cr);
}

} // namespace

Visualizer::Visualizer(XConnection &x, EventLoop &loop, Shell &shell, Services &services)
    : x_(x), loop_(loop), services_(services), window_(x, title, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_STRUCTURE_NOTIFY, false), keyboard_(x.conn()) {
    window_.make_managed(kVisualizerDefaultWindow, kVisualizerDefaultWindow);
    timer_ = loop.add_timer([this] { return open_ ? std::chrono::milliseconds(1000 / fps()) : idle_interval; }, [this] { step(); });
    loop.on_window(window_.id(), [this](const xcb_generic_event_t &event) { handle(event); });
    shell.track("visualizer", [this] { return open_; });
    shell.bind(ShellVerb::visualizer, [this] { toggle(); });
}

Visualizer::~Visualizer() { capture_.stop(); }

int Visualizer::fps() const { return std::clamp(kVisualizerBarFps, kVisualizerFpsMin, kVisualizerFpsMax); }

void Visualizer::toggle() {
    if (open_) {
        close();
    } else {
        open();
    }
}

void Visualizer::open() {
    const OutputGeometry &target = active_target_output(services_).geometry;
    int16_t x = static_cast<int16_t>(target.x + (target.width - kVisualizerDefaultWindow) / 2);
    int16_t y = static_cast<int16_t>(target.y + (target.height - kVisualizerDefaultWindow) / 2);
    window_.place({x, y, kVisualizerDefaultWindow, kVisualizerDefaultWindow});
    keyboard_.reload();
    stages_ = {};
    audio_size_ = 0;
    open_ = true;
    capture_.start();
    paint();
    window_.show(false);
    loop_.reschedule(timer_);
}

void Visualizer::close() {
    if (!open_) {
        return;
    }
    open_ = false;
    capture_.stop();
    window_.hide();
    window_.release();
    left_.clear();
    right_.clear();
    malloc_trim(0);
}

void Visualizer::step() {
    if (!open_) {
        return;
    }
    std::vector<float> left;
    std::vector<float> right;
    bool modified = false;
    capture_.take(left, right, modified);
    if (modified && static_cast<int>(left.size()) >= kVisualizerFragmentSize && static_cast<int>(right.size()) >= kVisualizerFragmentSize) {
        left_ = std::move(left);
        right_ = std::move(right);
        visualizer_fft(left_.data(), kVisualizerFragmentSize, kVisualizerFftScale, kVisualizerFftCutOff);
        visualizer_fft(right_.data(), kVisualizerFragmentSize, kVisualizerFftScale, kVisualizerFftCutOff);
        audio_size_ = kVisualizerFragmentSize;
    }
    if (audio_size_ > 0) {
        stages_.run(audio_size_, left_, right_, fps());
    }
    paint();
}

void Visualizer::handle(const xcb_generic_event_t &event) {
    if (!open_) {
        return;
    }
    if (window_.is_delete_message(event)) {
        close();
        return;
    }
    switch (event.response_type & ~0x80) {
    case XCB_EXPOSE:
        window_.present();
        break;
    case XCB_CONFIGURE_NOTIFY:
        if (window_.apply_configure(reinterpret_cast<const xcb_configure_notify_event_t &>(event))) {
            paint();
        }
        break;
    case XCB_KEY_PRESS: {
        const auto &press = reinterpret_cast<const xcb_key_press_event_t &>(event);
        if (to_neutral(keyboard_.press(press.detail, press.state)).kind == input::KeyKind::Escape) {
            close();
        }
        break;
    }
    default:
        break;
    }
}

void Visualizer::paint() {
    if (!open_ || window_.cr() == nullptr) {
        return;
    }
    cairo_t *cr = window_.cr();
    float width = window_.geometry().width;
    float height = window_.geometry().height;
    window_.clear();
    canvas_.bind(cr);
    canvas_.rect({0.0f, 0.0f, width, height}, palette::window_backdrop);
    int count = visualizer_bar_count(width);
    double total = count * kVisualizerBarWidth + (count - 1) * kVisualizerBarSpacing;
    double start = (width - total) / 2.0;
    double slot = kVisualizerBarWidth + kVisualizerBarSpacing;
    double radius = std::min(kVisualizerBarRadius, kVisualizerBarWidth * 0.5f);
    cairo_set_source_rgba(cr, palette::accent.r, palette::accent.g, palette::accent.b, kVisualizerBarOpacity);
    for (int i = 0; i < count; ++i) {
        float magnitude = 0.0f;
        if (stages_.ready()) {
            int texel = visualizer_bar_texel(i, count, stages_.size());
            magnitude = std::max(stages_.smooth_texel(0, texel), stages_.smooth_texel(1, texel));
        }
        double bar_height = visualizer_bar_height(magnitude, height);
        top_rounded(cr, start + i * slot, height - bar_height, kVisualizerBarWidth, bar_height, std::min(radius, bar_height));
        cairo_fill(cr);
    }
    window_.present();
}

} // namespace astralia
