#include <chrono>
#include <cmath>

#include "check.h"

#include "core/animation.h"

namespace {

using astralia::AnimationManager;
using astralia::Easing;
using namespace std::chrono_literals;

void check_easing() {
    for (Easing e : {Easing::Linear, Easing::EaseOutQuad, Easing::EaseInOutCubic, Easing::EaseOutCubic, Easing::EaseInCubic, Easing::EaseOutBack, Easing::EaseInBack}) {
        test::check(std::fabs(astralia::applyEasing(e, 0.0f)) < 1e-5f, "easing starts at zero");
        test::check(std::fabs(astralia::applyEasing(e, 1.0f) - 1.0f) < 1e-5f, "easing ends at one");
    }
    test::check(astralia::applyEasing(Easing::EaseOutQuad, 0.5f) > 0.5f, "ease out quad leads");
    test::check(astralia::applyEasing(Easing::EaseInCubic, 0.5f) < 0.5f, "ease in cubic lags");
    test::check(astralia::applyEasing(Easing::EaseInBack, 0.5f) < 0.0f, "ease in back undershoots");
}

void check_interpolation() {
    AnimationManager mgr;
    float value = -1.0f;
    int completions = 0;
    auto now = std::chrono::steady_clock::now();
    mgr.animate(0.0f, 10.0f, 100.0f, Easing::Linear, [&](float v) { value = v; }, [&] { completions++; });
    mgr.tick(now);
    test::check(value == 0.0f, "first tick sits at the start");
    mgr.tick(now + 50ms);
    test::check(std::fabs(value - 5.0f) < 1e-3f, "midpoint");
    test::check(completions == 0 && mgr.hasActive(), "still running at midpoint");
    mgr.tick(now + 150ms);
    test::check(std::fabs(value - 10.0f) < 1e-3f, "end value");
    test::check(completions == 1 && !mgr.hasActive(), "completes once and clears");
}

void check_zero_duration() {
    AnimationManager mgr;
    float value = -1.0f;
    int completions = 0;
    mgr.animate(0.0f, 5.0f, 0.0f, Easing::Linear, [&](float v) { value = v; }, [&] { completions++; });
    test::check(value == 5.0f && completions == 1 && !mgr.hasActive(), "zero duration snaps");
}

void check_owner() {
    AnimationManager mgr;
    float first = -1.0f;
    float second = -1.0f;
    auto now = std::chrono::steady_clock::now();
    mgr.animate(0.0f, 10.0f, 100.0f, Easing::Linear, [&](float v) { first = v; }, {}, 1);
    mgr.animate(0.0f, 20.0f, 100.0f, Easing::Linear, [&](float v) { second = v; }, {}, 1);
    mgr.tick(now + 150ms);
    test::check(first == -1.0f && second == 20.0f, "same owner replaces the prior animation");

    float cancelled = -1.0f;
    bool completed = false;
    mgr.animate(0.0f, 10.0f, 100.0f, Easing::Linear, [&](float v) { cancelled = v; }, [&] { completed = true; }, 2);
    mgr.cancelForOwner(2);
    mgr.tick(now + 400ms);
    test::check(!mgr.hasActive() && cancelled == -1.0f && !completed, "cancel drops the animation silently");
}

void check_instant() {
    AnimationManager mgr;
    float value = -1.0f;
    int completions = 0;
    astralia::animation_set_instant(true);
    test::check(astralia::animation_instant(), "flag reads back");
    mgr.animate(0.0f, 7.0f, 500.0f, Easing::EaseOutCubic, [&](float v) { value = v; }, [&] { completions++; });
    astralia::animation_set_instant(false);
    test::check(value == 7.0f && completions == 1 && !mgr.hasActive(), "instant mode snaps and keeps no entry");
    test::check(!astralia::animation_instant(), "flag clears");
}

} // namespace

void check_animation() {
    check_easing();
    check_interpolation();
    check_zero_duration();
    check_owner();
    check_instant();
}
