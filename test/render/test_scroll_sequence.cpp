#include <chrono>

#include "core/animation.h"

#include "render/scroll_sequence.h"

#include "check.h"

namespace {

using astralia::AnimationManager;
using astralia::ScrollSequence;
using std::chrono::seconds;
using std::chrono::steady_clock;

struct Instant {
    Instant() { astralia::animation_set_instant(true); }
    ~Instant() { astralia::animation_set_instant(false); }
};

void tick(AnimationManager &animations, int s) {
    animations.tick(steady_clock::now() + seconds(s));
}

void check_open() {
    AnimationManager animations;
    ScrollSequence sequence(animations, 10);
    sequence.open();
    test::check(sequence.slide() == 1.0f && sequence.scroll() == 0.0f && sequence.grid() == 0.0f &&
                    animations.hasActive(),
                "opening starts fully out and closed");
    tick(animations, 1);
    test::check(sequence.slide() == 0.0f && sequence.scroll() == 0.0f && !sequence.settled(),
                "the scroll slides in first");
    tick(animations, 2);
    test::check(sequence.scroll() == 1.0f && sequence.grid() == 0.0f && sequence.settled(),
                "then it unrolls");
    tick(animations, 3);
    test::check(sequence.grid() == 1.0f && !animations.hasActive(), "then the grid appears");
}

void check_close() {
    AnimationManager animations;
    ScrollSequence sequence(animations, 10);
    sequence.open();
    tick(animations, 1);
    tick(animations, 2);
    tick(animations, 3);
    int done = 0;
    sequence.close([&] { ++done; });
    test::check(sequence.closing() && done == 0, "closing is under way");
    tick(animations, 4);
    test::check(sequence.grid() == 0.0f && sequence.scroll() == 1.0f, "the grid goes first");
    tick(animations, 5);
    test::check(sequence.scroll() == 0.0f && sequence.slide() == 0.0f && done == 0,
                "then the scroll rolls up");
    tick(animations, 6);
    test::check(sequence.slide() == 1.0f && done == 1 && !sequence.closing() &&
                    !animations.hasActive(),
                "then it slides out and reports once");

    sequence.open();
    tick(animations, 7);
    tick(animations, 8);
    tick(animations, 9);
    sequence.close([&] { ++done; });
    sequence.close([&] { ++done; });
    for (int s = 10; s < 14; ++s) {
        tick(animations, s);
    }
    test::check(done == 2, "a second close request while closing is ignored");
}

void check_interrupts() {
    AnimationManager animations;
    ScrollSequence sequence(animations, 10);
    int done = 0;
    sequence.open();
    sequence.close([&] { ++done; });
    test::check(done == 1 && sequence.scroll() == 0.0f && sequence.slide() == 1.0f,
                "closing before the rise starts finishes at once");

    sequence.open();
    animations.tick(steady_clock::now() + std::chrono::milliseconds(100));
    test::check(sequence.slide() > 0.0f && sequence.slide() < 1.0f, "the scroll is part way in");
    sequence.close([&] { ++done; });
    test::check(sequence.scroll() == 0.0f && done == 1,
                "closing during the rise never unrolls the scroll");
    tick(animations, 2);
    tick(animations, 3);
    test::check(done == 2 && sequence.scroll() == 0.0f && sequence.slide() == 1.0f,
                "and the close still completes");

    AnimationManager other;
    ScrollSequence again(other, 10);
    int finished = 0;
    again.open();
    tick(other, 1);
    tick(other, 2);
    tick(other, 3);
    again.close([&] { ++finished; });
    tick(other, 4);
    again.open();
    for (int s = 5; s < 9; ++s) {
        tick(other, s);
    }
    test::check(finished == 0 && !again.closing() && again.settled() && again.grid() == 1.0f,
                "opening again cancels a close in flight");
}

void check_instant() {
    Instant instant;
    AnimationManager animations;
    ScrollSequence sequence(animations, 10);
    sequence.open();
    test::check(sequence.settled() && sequence.grid() == 1.0f && !animations.hasActive(),
                "instant mode opens straight to the end");
    int done = 0;
    sequence.close([&] { ++done; });
    test::check(done == 1 && sequence.slide() == 1.0f && !sequence.closing(),
                "and closes inside the call");
}

} // namespace

void check_scroll_sequence() {
    check_open();
    check_close();
    check_interrupts();
    check_instant();
}
