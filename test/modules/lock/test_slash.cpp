#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "check.h"
#include "render/recording_canvas.h"

#include "config/lock_config.h"

#include "modules/lock/slash.h"
#include "modules/lock/transition_view.h"

namespace {

using astralia::SlashFrame;
using astralia::SlashPlan;
using astralia::SlashPolygon;

float total_area(const SlashFrame &frame) {
    float sum = 0.0f;
    for (const SlashPolygon &piece : frame.desktop) {
        sum += astralia::slash_polygon_area(piece);
    }
    return sum;
}

bool inside(const SlashFrame &frame, float w, float h) {
    return std::ranges::all_of(frame.desktop, [&](const SlashPolygon &piece) {
        return std::ranges::all_of(piece, [&](const astralia::ui::Point &p) {
            return p.x >= -0.01f && p.y >= -0.01f && p.x <= w + 0.01f && p.y <= h + 0.01f;
        });
    });
}

} // namespace

void check_slash() {
    constexpr float w = 2560.0f;
    constexpr float h = 1440.0f;
    constexpr float land = kLockSlashFinalStart + kLockSlashFinalDrawSpan;
    constexpr float star_step = kLockStarAdvanceMs / kLockSlashMs;
    SlashPlan plan(7, w, h);
    const std::vector<astralia::SlashCut> &cuts = plan.cuts();
    const float last_land = cuts.back().land;

    SlashFrame first = plan.sample(0.0f);
    test::check(first.desktop.size() == 1 && std::fabs(total_area(first) - w * h) < 1.0f,
                "progress 0 is one full-screen desktop region");
    test::check(first.marks.empty() && first.strikes.empty() && first.flash == 0.0f && !first.shock,
                "progress 0 draws nothing over the desktop");

    SlashFrame last = plan.sample(1.0f);
    test::check(last.desktop.empty() && last.marks.empty() && last.strikes.empty() &&
                    last.rays.empty() && last.flash == 0.0f && !last.shock,
                "progress 1 leaves no desktop and nothing else");

    SlashFrame same = SlashPlan(7, w, h).sample(0.8f);
    SlashFrame mid = plan.sample(0.8f);
    test::check(same.desktop.size() == mid.desktop.size() && same.desktop[0] == mid.desktop[0],
                "the same seed gives the same frame");
    SlashFrame other = SlashPlan(8, w, h).sample(last_land + 0.01f);
    SlashFrame seven = plan.sample(last_land + 0.01f);
    test::check(other.marks[0].a == seven.marks[0].a && other.marks[0].b == seven.marks[0].b,
                "the cutmarks follow the reference order whatever the seed");

    bool monotonic = true;
    bool bounded = true;
    float previous = w * h + 1.0f;
    for (int i = 0; i <= 200; ++i) {
        float p = static_cast<float>(i) / 200.0f;
        SlashFrame frame = plan.sample(p);
        float area = total_area(frame);
        monotonic = monotonic && area <= previous + 1.0f;
        bounded = bounded && (p > land + kLockSlashShatterHold || inside(frame, w, h));
        previous = area;
    }
    test::check(monotonic, "the desktop area never grows with progress");
    test::check(bounded, "every desktop region stays inside the output until the shards fall");

    test::check(plan.sample(kLockSlashFirstStart - 0.001f).strikes.empty() &&
                    plan.sample(kLockSlashFirstStart + 0.005f).strikes.size() == 1 &&
                    plan.sample(kLockSlashFirstStart + star_step + 0.005f).strikes.size() == 2 &&
                    star_step < kLockSlashDrawSpan,
                "the cutmark slashes strike one after another, overlapping slightly");
    test::check(plan.sample(kLockSlashFirstStart + star_step + 0.005f).marks.size() == 2,
                "each strike leaves a cutmark");

    test::check(cuts.size() == static_cast<size_t>(kLockSlashCount) &&
                    std::ranges::is_sorted(cuts, {}, &astralia::SlashCut::start),
                "the slashes strike in the order of the reference clip");
    test::check(cuts[0].a.x < cuts[0].b.x && cuts[0].a.y < cuts[0].b.y,
                "the first star stroke goes down to the right");
    test::check(cuts[1].a.x > cuts[1].b.x && cuts[1].a.y > cuts[1].b.y,
                "the second star stroke goes up to the left");
    test::check(cuts[2].a.x < cuts[2].b.x && std::fabs(cuts[2].a.y - cuts[2].b.y) < 1.0f,
                "the third star stroke runs level to the right");
    test::check(cuts[3].a.x > cuts[3].b.x && cuts[3].a.y < cuts[3].b.y,
                "the fourth star stroke goes down to the left");
    test::check(cuts[4].a.x < cuts[4].b.x && cuts[4].a.y > cuts[4].b.y,
                "the fifth star stroke goes up to the right");
    test::check(cuts[kLockStarCount].a.y > cuts[kLockStarCount].b.y &&
                    std::fabs(cuts[kLockStarCount].a.x - cuts[kLockStarCount].b.x) < w * 0.1f,
                "the flurry opens with a stroke straight up");

    bool spans = true;
    bool near_centre = true;
    std::vector<float> angles;
    for (const astralia::SlashCut &cut : cuts) {
        auto on_edge = [&](astralia::ui::Point p) {
            return std::fabs(p.x) < 0.5f || std::fabs(p.x - w) < 0.5f || std::fabs(p.y) < 0.5f ||
                   std::fabs(p.y - h) < 0.5f;
        };
        spans = spans && on_edge(cut.a) && on_edge(cut.b);
        float miss = std::fabs(cut.direction.x * (h * 0.5f - cut.center.y) -
                               cut.direction.y * (w * 0.5f - cut.center.x));
        near_centre = near_centre && miss < h * 0.4f;
        float angle = std::atan2(cut.direction.y, cut.direction.x);
        angles.push_back(angle < 0.0f ? angle + std::numbers::pi_v<float> : angle);
    }
    test::check(spans, "every slash spans the display from edge to edge");
    test::check(near_centre, "every slash passes near the centre");
    std::ranges::sort(angles);
    float gap = std::numbers::pi_v<float> - angles.back() + angles.front();
    for (size_t i = 1; i < angles.size(); ++i) {
        gap = std::min(gap, angles[i] - angles[i - 1]);
    }
    test::check(gap > 0.07f, "the slashes fan out over every angle");
    bool same_plan = true;
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        SlashPlan again(seed, w, h);
        for (size_t i = 0; i < cuts.size(); ++i) {
            same_plan =
                same_plan && again.cuts()[i].a == cuts[i].a && again.cuts()[i].b == cuts[i].b;
        }
    }
    test::check(same_plan, "every seed plays the same slashes");

    SlashFrame waiting = plan.sample(kLockSlashFinalStart - 0.01f);
    test::check(waiting.desktop.size() == 1 && std::fabs(total_area(waiting) - w * h) < 1.0f,
                "the cutmarks leave the desktop whole");
    test::check(waiting.marks.size() == static_cast<size_t>(kLockSlashCount) &&
                    waiting.strikes.empty(),
                "during the wait only the cutmarks remain");
    bool thin = std::ranges::all_of(waiting.marks, [](const astralia::SlashMark &mark) {
        return mark.width == kLockCutmarkWidth && mark.alpha == kLockCutmarkAlpha;
    });
    test::check(thin, "the cutmarks are thin and steady");
    test::check((kLockSlashFinalStart - last_land) * kLockSlashMs >= 500.0f,
                "the cutmarks wait a long beat before the final slash");

    SlashFrame drawing = plan.sample(kLockSlashFinalStart + kLockSlashFinalDrawSpan * 0.5f);
    test::check(drawing.desktop.size() == 1 && drawing.marks.size() == kLockSlashCount &&
                    drawing.strikes.size() == 1 && drawing.strikes[0].thick == kLockFinalThick &&
                    drawing.strikes[0].progress > 0.0f && drawing.strikes[0].progress < 1.0f,
                "the final slash is drawn on before anything breaks");
    test::check(drawing.strikes[0].a.x < drawing.strikes[0].b.x &&
                    drawing.strikes[0].a.y > drawing.strikes[0].b.y,
                "the final slash runs from lower left to upper right");

    float slope = (drawing.strikes[0].a.y - drawing.strikes[0].b.y) /
                  (drawing.strikes[0].b.x - drawing.strikes[0].a.x);
    test::check(slope > 0.05f && slope < 0.3f,
                "the final slash rises at a shallow tilt, not corner to corner");

    test::check(plan.sample(0.0f).dim == 0.0f &&
                    std::fabs(plan.sample(kLockSlashFinalStart - 0.01f).dim - kLockDimPeak) <
                        0.01f &&
                    plan.sample(land + kLockSlashDimLift + 0.01f).dim == 0.0f,
                "the desktop dims during the flurry and lifts after the final slash");

    SlashFrame striking = plan.sample(kLockSlashFirstStart + 0.01f);
    test::check(striking.beams.size() == 1 && striking.beams[0].width > 0.0f &&
                    striking.beams[0].width <= kLockBeamPeakFrac * h + 0.01f,
                "a strike carries a tapered beam that is thick as it is struck");
    SlashFrame ending = plan.sample(kLockSlashFirstStart + kLockSlashDrawSpan - 0.001f);
    test::check(ending.beams.empty() || ending.beams[0].width < striking.beams[0].width,
                "the beam thins out as the strike ends");

    SlashFrame before = plan.sample(land - 0.001f);
    SlashFrame after = plan.sample(land + 0.001f);
    test::check(!before.shattered && before.sources.size() == before.desktop.size() &&
                    after.shattered && after.sources.size() == after.desktop.size() &&
                    after.desktop.size() > 1,
                "the desktop breaks into wallpaper shards at the final slash");
    constexpr float tail = kLockSlashShatterHold + kLockSlashShatterSpread + kLockSlashFallSpan;
    test::check(plan.sample(land + tail + 0.001f).desktop.empty() &&
                    !plan.sample(land + tail * 0.5f).desktop.empty() &&
                    tail * kLockSlashMs < 600.0f,
                "the shards fall away within about half a second");

    SlashFrame whiteout = plan.sample(land + 0.001f);
    float final_beam = 0.0f;
    for (const astralia::SlashBeam &beam : whiteout.beams) {
        final_beam = std::max(final_beam, beam.width);
    }
    test::check(final_beam > kLockFinalBeamFrac * h * 0.9f,
                "the final slash lands as a beam about a third of the screen tall");
    test::check(!plan.sample(land + kLockSlashFlashSpan * 0.4f).slivers.empty() &&
                    plan.sample(land + kLockSlashSliverSpan + 0.01f).slivers.empty(),
                "slivers fly off the final slash and are gone after a moment");
    test::check(plan.sample(land + kLockSlashFlashHold * 0.5f).flash == 1.0f &&
                    plan.sample(land + kLockSlashFlashSpan * 0.6f).flash <
                        plan.sample(land + kLockSlashFlashHold + 0.001f).flash,
                "the flash holds for a frame, then decays");

    SlashFrame impact = plan.sample(land + kLockSlashImpactSpan * 0.3f);
    test::check(impact.flash > 0.0f && !impact.rays.empty() && impact.shock,
                "the final slash lands with the impact flash, rays and shock");
    test::check(impact.desktop.size() >= kLockSlashCount + 2u && impact.marks.empty(),
                "the screen shatters along the cutmarks and the final slash");
    test::check(plan.sample(0.02f).flash == 0.0f && plan.sample(land - 0.01f).flash == 0.0f &&
                    plan.sample(land + kLockSlashFlashSpan + 0.01f).flash == 0.0f &&
                    plan.sample(0.9f).rays.empty() && !plan.sample(0.9f).shock,
                "no flash before the final slash or after its impact");

    bool strikes_valid = true;
    for (int i = 0; i <= 200; ++i) {
        for (const astralia::SlashStrike &strike :
             plan.sample(static_cast<float>(i) / 200.0f).strikes) {
            strikes_valid = strikes_valid && strike.progress >= 0.0f && strike.progress <= 1.0f &&
                            strike.intensity >= 0.0f;
        }
    }
    test::check(strikes_valid, "strikes have a valid progress and intensity");

    test::check(astralia::slash_run_progress(0.0f) == 0.0f &&
                    astralia::slash_run_progress(kLockSlashMs * 2.0f) == 1.0f,
                "the run goes from 0 to 1 and clamps");

    SlashPlan empty(1, 0.0f, 0.0f);
    SlashPlan tiny(1, 1.0f, 1.0f);
    for (int i = 0; i <= 10; ++i) {
        empty.sample(static_cast<float>(i) / 10.0f);
        tiny.sample(static_cast<float>(i) / 10.0f);
    }
    test::check(empty.sample(0.5f).desktop.empty(), "a zero-sized output has no desktop region");

    test::RecordingCanvas canvas;
    auto base = [&] { canvas.rect({0.0f, 0.0f, w, h}, {}); };
    astralia::paint_lock_transition(canvas, plan, 0.0f, base);
    test::check(canvas.ops.size() == 2 && canvas.ops[0].kind == test::Op::Kind::rect &&
                    canvas.ops[1].kind == test::Op::Kind::cut_polygon,
                "progress 0 paints the base and cuts the whole screen");

    int strikes = 0;
    int shocks = 0;
    astralia::LockThunder thunder;
    thunder.strike = [&](const astralia::SlashStrike &) { ++strikes; };
    thunder.shock = [&](const astralia::SlashShock &) { ++shocks; };
    canvas.ops.clear();
    astralia::paint_lock_transition(canvas, plan, kLockSlashFirstStart + 0.005f, base, thunder);
    test::check(strikes == 1 && shocks == 0 && canvas.count(test::Op::Kind::polygon) >= 4,
                "a strike goes to the thunder hook, beside its beam, cutmark and the dimming");
    canvas.ops.clear();
    strikes = 0;
    astralia::paint_lock_transition(canvas, plan, land + kLockSlashImpactSpan * 0.3f, base,
                                    thunder);
    test::check(shocks == 1 && strikes == 1 && canvas.ops[0].kind == test::Op::Kind::rect &&
                    canvas.ops.back().kind == test::Op::Kind::rect &&
                    canvas.count(test::Op::Kind::snapshot_polygon) >= kLockSlashCount + 2u &&
                    canvas.count(test::Op::Kind::cut_polygon) == 0,
                "the impact frame draws the wallpaper shards, calls the thunder, then flashes");
    canvas.ops.clear();
    astralia::paint_lock_transition(canvas, plan, kLockSlashFirstStart + 0.005f, base);
    test::check(canvas.count(test::Op::Kind::polygon) > 1,
                "without a thunder hook the strike falls back to plain strokes");
}
