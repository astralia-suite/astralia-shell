#include <chrono>
#include <string>
#include <vector>

#include "modules/idle/bounce.h"
#include "modules/idle/clock.h"

#include "check.h"

namespace {

using std::chrono::seconds;

}

void check_idle() {
    using namespace astralia;
    using test::check;
    auto start = astralia::idle::Clock::Time{};

    astralia::idle::Clock clock;
    check(!clock.idle("A", 0, start + seconds(100)), "an unknown output is never idle");
    clock.reset({"A", "B"}, start);
    check(clock.known("A") && clock.known("B"), "a reset seeds every output");
    check(!clock.idle("A", 150, start + seconds(150)) && clock.idle("A", 150, start + seconds(151)),
          "idle begins strictly after the timeout");
    clock.activity("A", start + seconds(140));
    check(!clock.idle("A", 150, start + seconds(200)) && clock.idle("B", 150, start + seconds(200)),
          "activity on one output leaves the other idle");

    Config config;
    astralia::idle::Clock fresh;
    fresh.reset({"A"}, start);
    astralia::idle::Verdict early =
        astralia::idle::idle_verdict(fresh, config, "A", start + seconds(100));
    check(!early.ambient && !early.screensaver, "nothing shows before the ambient timeout");
    astralia::idle::Verdict ambient =
        astralia::idle::idle_verdict(fresh, config, "A", start + seconds(200));
    check(ambient.ambient && !ambient.screensaver, "the ambient overlay comes first");
    astralia::idle::Verdict saver =
        astralia::idle::idle_verdict(fresh, config, "A", start + seconds(400));
    check(saver.ambient && saver.screensaver, "the screensaver follows");
    config.idle_enabled = false;
    astralia::idle::Verdict off =
        astralia::idle::idle_verdict(fresh, config, "A", start + seconds(400));
    check(!off.ambient && !off.screensaver, "disabled idle management shows nothing");
    config.idle_enabled = true;
    config.monitor_overrides["A"].enabled = true;
    config.monitor_overrides["A"].ambient_timeout = 10;
    config.monitor_overrides["A"].screensaver_enabled = false;
    astralia::idle::Verdict overridden =
        astralia::idle::idle_verdict(fresh, config, "A", start + seconds(400));
    check(overridden.ambient && !overridden.screensaver,
          "an override picks its own timeout and switches");

    Config before;
    Config after = before;
    check(!astralia::idle::policy_changed(before, after), "equal policies are unchanged");
    after.screensaver_timeout = 30;
    check(astralia::idle::policy_changed(before, after), "a new timeout is a policy change");
    after = before;
    after.idle_enabled = false;
    check(astralia::idle::policy_changed(before, after),
          "toggling idle management is a policy change");
    after = before;
    after.autohide = true;
    check(!astralia::idle::policy_changed(before, after), "unrelated settings are not");

    astralia::idle::Bounce bounce{10.0f, 10.0f, 100.0f, 50.0f};
    astralia::idle::bounce_step(bounce, 0.5f, 1000.0f, 800.0f, 200.0f);
    check(bounce.x == 60.0f && bounce.y == 35.0f && bounce.vx == 100.0f,
          "the logo moves by velocity times time");
    astralia::idle::Bounce right{790.0f, 10.0f, 100.0f, 50.0f};
    astralia::idle::bounce_step(right, 1.0f, 1000.0f, 800.0f, 200.0f);
    check(right.x == 800.0f && right.vx == -100.0f, "the right edge reverses horizontal motion");
    astralia::idle::Bounce top{10.0f, 5.0f, 100.0f, -50.0f};
    astralia::idle::bounce_step(top, 1.0f, 1000.0f, 800.0f, 200.0f);
    check(top.y == 0.0f && top.vy == 50.0f, "the top edge reverses vertical motion");
}
