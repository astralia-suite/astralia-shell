#include <dirent.h>
#include <string>
#include <unistd.h>

#include "core/async_process.h"
#include "core/poll_reactor.h"

#include "check.h"
#include "core/pump.h"

namespace {

int count_open_fds() {
    DIR *d = opendir("/proc/self/fd");
    if (!d) {
        return -1;
    }
    int count = 0;
    while (readdir(d) != nullptr) {
        ++count;
    }
    closedir(d);
    return count;
}

} // namespace

void check_async_process() {
    using test::check;
    auto created = astralia::PollReactor::create();
    check(created.has_value(), "reactor is created");
    if (!created) {
        return;
    }
    astralia::PollReactor reactor = std::move(*created);

    {
        astralia::AsyncProcess proc(reactor);
        std::string output;
        bool done = false;
        check(proc.start({"echo", "hello"}, [&](std::string text) {
            output = std::move(text);
            done = true;
        }),
              "echo starts");
        check(proc.running(), "a started process is running");
        check(test::pump_until(reactor, [&] { return done; }), "echo finishes");
        check(output == "hello\n", "output is delivered");
        check(!proc.running(), "a finished process is not running");
    }

    {
        astralia::AsyncProcess proc(reactor);
        bool called = false;
        check(!proc.start({}, [&](std::string) { called = true; }), "an empty command is rejected");
        check(!proc.start({"definitely-not-a-real-command"}, [&](std::string) { called = true; }),
              "an unknown command is rejected");
        reactor.run_once(50);
        check(!called, "a rejected command never reports");
    }

    {
        astralia::AsyncProcess sleeper(reactor);
        bool called = false;
        check(sleeper.start({"sleep", "5"}, [&](std::string) { called = true; }), "sleep starts");
        sleeper.cancel();
        check(!sleeper.running(), "cancel clears running");
        reactor.run_once(50);
        check(!called, "a cancelled process never reports");
    }

    {
        astralia::AsyncProcess proc(reactor);
        std::string output;
        for (int i = 0; i < 10; ++i) {
            check(proc.start({"sleep", "1"}, [&](std::string) { output = "stale"; }), "slow job starts");
            bool done = false;
            check(proc.start({"echo", "fresh"}, [&](std::string text) {
                output = std::move(text);
                done = true;
            }),
                  "restart cancels the previous job");
            check(test::pump_until(reactor, [&] { return done; }), "restarted job finishes");
            check(output == "fresh\n", "only the newest job reports");
        }
    }

    {
        astralia::AsyncProcess proc(reactor);
        bool done = false;
        proc.start({"echo", "warmup"}, [&](std::string) { done = true; });
        test::pump_until(reactor, [&] { return done; });
        int before = count_open_fds();
        for (int i = 0; i < 30; ++i) {
            done = false;
            proc.start({"echo", "x"}, [&](std::string) { done = true; });
            check(test::pump_until(reactor, [&] { return done; }), "repeat job finishes");
        }
        check(count_open_fds() - before < 5, "repeated jobs do not leak descriptors");
    }
}
