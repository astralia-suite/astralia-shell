#include <algorithm>
#include <cmath>

#include "check.h"

#include "config/launcher_config.h"

#include "modules/launcher/entrance.h"

namespace {

using astralia::EntranceFrame;

const astralia::ui::Box box{610.0f, 400.0f, 700.0f, 300.0f};

EntranceFrame at(float ms, float seed = 3.0f) {
    return astralia::entrance_sample({ms / astralia::launcher_config::entrance_ms, seed}, box);
}

float lit_fraction(const EntranceFrame &frame) {
    float lit = 0.0f;
    for (const astralia::EntranceBolt &bolt : frame.bolts) {
        if (bolt.amp == 4.0f) {
            lit += bolt.progress * std::hypot(bolt.bx - bolt.ax, bolt.by - bolt.ay);
        }
    }
    return lit / (2.0f * (box.w + box.h));
}

float sky_intensity(const EntranceFrame &frame) {
    float best = 0.0f;
    for (const astralia::EntranceBolt &bolt : frame.bolts) {
        if (bolt.amp == 22.0f) {
            best = std::max(best, bolt.intensity);
        }
    }
    return best;
}

void check_ends() {
    EntranceFrame start = at(0.0f);
    test::check(start.opacity == 0.0f && start.bolts.empty() && start.bursts.empty() &&
                    start.scrim == 0.0f && start.flash == 0.0f,
                "the entrance starts empty and hidden");
    EntranceFrame done = at(astralia::launcher_config::entrance_ms);
    test::check(done.opacity == 1.0f && done.bolts.empty() && done.bursts.empty() &&
                    done.scrim == 0.0f && done.flash == 0.0f,
                "the entrance ends on the plain card");
}

void check_trace() {
    EntranceFrame tracing = at(200.0f);
    bool on_border = !tracing.bolts.empty();
    for (const astralia::EntranceBolt &bolt : tracing.bolts) {
        on_border = on_border && bolt.ax >= box.x - 25.0f && bolt.ax <= box.x + box.w + 25.0f &&
                    bolt.ay >= box.y - 25.0f && bolt.ay <= box.y + box.h + 25.0f;
    }
    test::check(on_border, "the bolt stays on the card outline");
    test::check(tracing.opacity == 0.0f && tracing.bursts.empty() && tracing.flash == 0.0f,
                "the card stays hidden and nothing flashes while the bolt traces it");

    float last = 0.0f;
    bool grows = true;
    for (float ms = 10.0f; ms <= 400.0f; ms += 10.0f) {
        float lit = 0.0f;
        for (const astralia::EntranceBolt &bolt : at(ms).bolts) {
            if (bolt.amp == 4.0f) {
                lit += bolt.progress;
            }
        }
        grows = grows && lit >= last;
        last = lit;
    }
    test::check(grows, "the traced outline only grows");
    test::check(at(430.0f).bolts.size() >= 4, "the whole outline is lit when the trace ends");
    test::check(lit_fraction(at(380.0f)) >= 0.99f, "the trace closes the outline when it ends");

    float widest = 0.0f;
    float before = 0.0f;
    for (float ms = 30.0f; ms <= 380.0f; ms += 8.0f) {
        float lit = lit_fraction(at(ms));
        widest = std::max(widest, lit - before);
        before = lit;
    }
    test::check(widest < 0.12f, "the trace never jumps between frames");
    test::check(lit_fraction(at(38.0f)) < 0.01f &&
                    lit_fraction(at(380.0f)) - lit_fraction(at(372.0f)) < 0.03f,
                "the trace eases in and eases out");

    bool stitched = true;
    for (const astralia::EntranceBolt &bolt : at(250.0f).bolts) {
        stitched = stitched && (bolt.stitch == (bolt.amp == 4.0f || bolt.amp == 6.0f));
    }
    for (const astralia::EntranceBolt &bolt : at(450.0f).bolts) {
        stitched = stitched && (bolt.stitch == (bolt.amp == 4.0f || bolt.amp == 6.0f));
    }
    test::check(stitched, "only the outline and its sparks use the stitched bolt");
}

void check_sparks() {
    bool grows = false;
    for (const astralia::EntranceBolt &bolt : at(50.0f).bolts) {
        grows = grows || (bolt.amp == 6.0f && bolt.progress > 0.0f && bolt.progress < 1.0f);
    }
    test::check(grows, "a corner spark grows instead of popping in");

    int closing = 0;
    for (const astralia::EntranceBolt &bolt : at(385.0f).bolts) {
        if (bolt.amp == 6.0f && bolt.ax == box.x && bolt.ay == box.y) {
            ++closing;
        }
    }
    test::check(closing == 1, "a spark marks the corner where the trace closes");
    int lingering = 0;
    for (const astralia::EntranceBolt &bolt : at(700.0f).bolts) {
        lingering += bolt.amp == 6.0f ? 1 : 0;
    }
    test::check(lingering == 0, "the sparks are gone before the entrance ends");
}

void check_steady_trace() {
    bool steady = true;
    for (float ms = 0.0f; ms <= 332.0f; ms += 8.0f) {
        for (const astralia::EntranceBolt &bolt : at(ms).bolts) {
            if (bolt.amp == 4.0f) {
                steady = steady && bolt.intensity == 1.0f;
            }
        }
    }
    test::check(steady, "the outline bolt never flickers while it traces");
}

void check_fall() {
    EntranceFrame fall = at(450.0f);
    bool charged = false;
    bool falling = false;
    for (const astralia::EntranceBolt &bolt : fall.bolts) {
        charged = charged || (bolt.amp == 4.0f && bolt.intensity > 1.0f);
        falling = falling || (bolt.amp == 22.0f && bolt.ay < 0.0f && bolt.by == box.y &&
                              bolt.progress > 0.0f && bolt.progress < 1.0f);
    }
    test::check(fall.opacity == 0.0f && fall.bursts.empty() && fall.flash == 0.0f,
                "the card and the flash wait for the strike");
    test::check(charged && falling,
                "a bolt falls from the top onto the card while the outline charges");
}

void check_strike() {
    EntranceFrame before = at(480.0f);
    test::check(before.opacity == 0.0f && before.flash == 0.0f,
                "nothing lands before the bolt arrives");
    EntranceFrame strike = at(500.0f);
    test::check(strike.opacity == 1.0f && strike.flash > 0.4f,
                "the card snaps in whole under a bright flash on impact");
    bool landed = false;
    for (const astralia::EntranceBolt &bolt : strike.bolts) {
        landed = landed || (bolt.amp == 22.0f && bolt.progress == 1.0f && bolt.by == box.y);
    }
    test::check(landed, "the bolt has reached the card top");
    test::check(at(560.0f).flash < strike.flash && at(650.0f).flash == 0.0f,
                "the flash decays quickly");
    test::check(sky_intensity(at(700.0f)) < sky_intensity(strike),
                "the sky bolt fades after the strike");
}

void check_blast() {
    EntranceFrame blast = at(500.0f);
    test::check(blast.bursts.size() == 1 && blast.bursts[0].box.x == box.x &&
                    blast.bursts[0].box.w == box.w && blast.bursts[0].box.h == box.h &&
                    blast.bursts[0].reach > 0.0f,
                "the burst follows the card rectangle and reaches past it");
    EntranceFrame later = at(600.0f);
    test::check(later.bursts.size() == 1 && later.bursts[0].progress > blast.bursts[0].progress,
                "there is a single burst and it keeps spreading");
    test::check(at(540.0f).bursts[0].progress > 0.1f, "the burst moves quickly");
    test::check(at(780.0f).bursts.empty(), "the burst is over before the entrance ends");
}

void check_scrim() {
    test::check(at(0.0f).scrim == 0.0f, "the backdrop starts undimmed");
    float peak = 0.0f;
    bool gentle = true;
    float before = 0.0f;
    for (float ms = 0.0f; ms <= astralia::launcher_config::entrance_ms; ms += 8.0f) {
        float scrim = at(ms).scrim;
        peak = std::max(peak, scrim);
        gentle = gentle && std::fabs(scrim - before) < 0.05f;
        before = scrim;
    }
    test::check(peak > 0.2f && peak <= 0.4f && gentle, "the backdrop dims and returns gradually");
}

void check_ranges() {
    bool sane = true;
    for (float ms = 0.0f; ms <= astralia::launcher_config::entrance_ms; ms += 5.0f) {
        EntranceFrame f = at(ms);
        sane =
            sane && (f.opacity == 0.0f || f.opacity == 1.0f) && f.flash >= 0.0f && f.flash <= 1.0f;
        for (const astralia::EntranceBolt &bolt : f.bolts) {
            sane = sane && std::isfinite(bolt.ax) && std::isfinite(bolt.by) &&
                   bolt.progress > 0.0f && bolt.progress <= 1.0f && bolt.intensity > 0.0f &&
                   bolt.intensity <= 1.5f;
        }
        for (const astralia::EntranceBurst &burst : f.bursts) {
            sane = sane && burst.reach > 0.0f && burst.progress >= 0.0f && burst.progress < 1.0f;
        }
    }
    test::check(sane, "every frame stays within range");
}

} // namespace

void check_launcher_entrance() {
    check_ends();
    check_trace();
    check_steady_trace();
    check_sparks();
    check_fall();
    check_strike();
    check_blast();
    check_scrim();
    check_ranges();
}
