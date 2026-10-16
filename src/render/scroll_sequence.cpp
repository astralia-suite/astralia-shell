#include "render/scroll_sequence.h"

namespace astralia {

void ScrollSequence::open() {
    closing_ = false;
    slide_ = 1.0f;
    scroll_ = 0.0f;
    grid_ = 0.0f;
    animations_.cancelForOwner(owner_ + 1);
    animations_.cancelForOwner(owner_ + 2);
    animations_.animate(
        slide_, 0.0f, timings_.slide_ms, Easing::EaseOutCubic, [this](float v) { slide_ = v; },
        [this] {
            if (closing_) {
                return;
            }
            animations_.animate(
                scroll_, 1.0f, timings_.scroll_ms, Easing::EaseOutCubic,
                [this](float v) { scroll_ = v; },
                [this] {
                    if (closing_) {
                        return;
                    }
                    animations_.animate(
                        grid_, 1.0f, timings_.grid_in_ms, Easing::EaseOutCubic,
                        [this](float v) { grid_ = v; }, {}, owner_ + 2);
                },
                owner_ + 1);
        },
        owner_);
}

void ScrollSequence::close(std::function<void()> done) {
    if (closing_) {
        return;
    }
    closing_ = true;
    animations_.animate(
        grid_, 0.0f, timings_.grid_out_ms * grid_, Easing::EaseInOutCubic,
        [this](float v) { grid_ = v; },
        [this, done] {
            animations_.animate(
                scroll_, 0.0f, timings_.scroll_ms * scroll_, Easing::EaseOutCubic,
                [this](float v) { scroll_ = v; },
                [this, done] {
                    animations_.animate(
                        slide_, 1.0f, timings_.slide_ms * (1.0f - slide_), Easing::EaseOutCubic,
                        [this](float v) { slide_ = v; },
                        [this, done] {
                            closing_ = false;
                            if (done) {
                                done();
                            }
                        },
                        owner_);
                },
                owner_ + 1);
        },
        owner_ + 2);
}

} // namespace astralia
