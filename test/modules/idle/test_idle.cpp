#include <chrono>
#include <string>
#include <vector>

#include "modules/idle/bounce.h"
#include "modules/idle/idle_clock.h"

#include "check.h"

namespace {

using std::chrono::seconds;

}

void check_idle() {
    using namespace astralia;
    using test::check;
    auto start = IdleClock::Time{};

    IdleClock clock;
    check(!clock.idle("A", 0, start + seconds(100)), "an unknown output is never idle");
    clock.reset({"A", "B"}, start);
    check(clock.known("A") && clock.known("B"), "a reset seeds every output");
    check(!clock.idle("A", 150, start + seconds(150)) && clock.idle("A", 150, start + seconds(151)),
          "idle begins strictly after the timeout");
    clock.activity("A", start + seconds(140));
    check(!clock.idle("A", 150, start + seconds(200)) && clock.idle("B", 150, start + seconds(200)),
          "activity on one output leaves the other idle");

    Config config;
    IdleClock fresh;
    fresh.reset({"A"}, start);
    IdleVerdict early = idle_verdict(fresh, config, "A", start + seconds(100));
    check(!early.ambient && !early.screensaver, "nothing shows before the ambient timeout");
    IdleVerdict ambient = idle_verdict(fresh, config, "A", start + seconds(200));
    check(ambient.ambient && !ambient.screensaver, "the ambient overlay comes first");
    IdleVerdict saver = idle_verdict(fresh, config, "A", start + seconds(400));
    check(saver.ambient && saver.screensaver, "the screensaver follows");
    config.idle_management_enabled = false;
    IdleVerdict off = idle_verdict(fresh, config, "A", start + seconds(400));
    check(!off.ambient && !off.screensaver, "disabled idle management shows nothing");
    config.idle_management_enabled = true;
    config.monitor_overrides["A"].enabled = true;
    config.monitor_overrides["A"].ambient_timeout_seconds = 10;
    config.monitor_overrides["A"].screensaver_enabled = false;
    IdleVerdict overridden = idle_verdict(fresh, config, "A", start + seconds(400));
    check(overridden.ambient && !overridden.screensaver,
          "an override picks its own timeout and switches");

    Config before;
    Config after = before;
    check(!idle_policy_changed(before, after), "equal policies are unchanged");
    after.screensaver_timeout_seconds = 30;
    check(idle_policy_changed(before, after), "a new timeout is a policy change");
    after = before;
    after.idle_management_enabled = false;
    check(idle_policy_changed(before, after), "toggling idle management is a policy change");
    after = before;
    after.autohide = true;
    check(!idle_policy_changed(before, after), "unrelated settings are not");

    Bounce bounce{10.0f, 10.0f, 100.0f, 50.0f};
    bounce_step(bounce, 0.5f, 1000.0f, 800.0f, 200.0f);
    check(bounce.x == 60.0f && bounce.y == 35.0f && bounce.vx == 100.0f,
          "the logo moves by velocity times time");
    Bounce right{790.0f, 10.0f, 100.0f, 50.0f};
    bounce_step(right, 1.0f, 1000.0f, 800.0f, 200.0f);
    check(right.x == 800.0f && right.vx == -100.0f, "the right edge reverses horizontal motion");
    Bounce top{10.0f, 5.0f, 100.0f, -50.0f};
    bounce_step(top, 1.0f, 1000.0f, 800.0f, 200.0f);
    check(top.y == 0.0f && top.vy == 50.0f, "the top edge reverses vertical motion");
}
