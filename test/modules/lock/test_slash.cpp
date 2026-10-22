#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "config/lock_config.h"

#include "modules/lock/slash.h"
#include "modules/lock/transition_view.h"

#include "check.h"
#include "render/recording_canvas.h"

namespace {

using astralia::lock::SlashFrame;
using astralia::lock::SlashPlan;
using astralia::lock::SlashPolygon;

float total_area(const astralia::lock::SlashFrame &frame) {
    float sum = 0.0f;
    for (const astralia::lock::SlashPolygon &piece : frame.desktop) {
        sum += astralia::lock::slash_polygon_area(piece);
    }
    return sum;
}

bool inside(const astralia::lock::SlashFrame &frame, float w, float h) {
    return std::ranges::all_of(frame.desktop, [&](const astralia::lock::SlashPolygon &piece) {
        return std::ranges::all_of(piece, [&](const astralia::ui::Point &p) {
            return p.x >= -0.01f && p.y >= -0.01f && p.x <= w + 0.01f && p.y <= h + 0.01f;
        });
    });
}

} // namespace

void check_slash() {
    constexpr float w = 2560.0f;
    constexpr float h = 1440.0f;
    constexpr float land =
        astralia::lock_config::kSlashFinalStart + astralia::lock_config::kSlashFinalDrawSpan;
    constexpr float star_step =
        astralia::lock_config::kStarAdvanceMs / astralia::lock_config::kSlashMs;
    astralia::lock::SlashPlan plan(7, w, h);
    const std::vector<astralia::lock::SlashCut> &cuts = plan.cuts();
    const float last_land = cuts.back().land;

    astralia::lock::SlashFrame first = plan.sample(0.0f);
    test::check(first.desktop.size() == 1 && std::fabs(total_area(first) - w * h) < 1.0f,
                "progress 0 is one full-screen desktop region");
    test::check(first.marks.empty() && first.strikes.empty() && first.flash == 0.0f && !first.shock,
                "progress 0 draws nothing over the desktop");

    astralia::lock::SlashFrame last = plan.sample(1.0f);
    test::check(last.desktop.empty() && last.marks.empty() && last.strikes.empty() &&
                    last.rays.empty() && last.flash == 0.0f && !last.shock,
                "progress 1 leaves no desktop and nothing else");

    astralia::lock::SlashFrame same = astralia::lock::SlashPlan(7, w, h).sample(0.8f);
    astralia::lock::SlashFrame mid = plan.sample(0.8f);
    test::check(same.desktop.size() == mid.desktop.size() && same.desktop[0] == mid.desktop[0],
                "the same seed gives the same frame");
    astralia::lock::SlashFrame other = astralia::lock::SlashPlan(8, w, h).sample(last_land + 0.01f);
    astralia::lock::SlashFrame seven = plan.sample(last_land + 0.01f);
    test::check(other.marks[0].a == seven.marks[0].a && other.marks[0].b == seven.marks[0].b,
                "the cutmarks follow the reference order whatever the seed");

    bool monotonic = true;
    bool bounded = true;
    float previous = w * h + 1.0f;
    for (int i = 0; i <= 200; ++i) {
        float p = static_cast<float>(i) / 200.0f;
        astralia::lock::SlashFrame frame = plan.sample(p);
        float area = total_area(frame);
        monotonic = monotonic && area <= previous + 1.0f;
        bounded =
            bounded && (p > land + astralia::lock_config::kSlashShatterHold || inside(frame, w, h));
        previous = area;
    }
    test::check(monotonic, "the desktop area never grows with progress");
    test::check(bounded, "every desktop region stays inside the output until the shards fall");

    test::check(plan.sample(astralia::lock_config::kSlashFirstStart - 0.001f).strikes.empty() &&
                    plan.sample(astralia::lock_config::kSlashFirstStart + 0.005f).strikes.size() ==
                        1 &&
                    plan.sample(astralia::lock_config::kSlashFirstStart + star_step + 0.005f)
                            .strikes.size() == 2 &&
                    star_step < astralia::lock_config::kSlashDrawSpan,
                "the cutmark slashes strike one after another, overlapping slightly");
    test::check(
        plan.sample(astralia::lock_config::kSlashFirstStart + star_step + 0.005f).marks.size() == 2,
        "each strike leaves a cutmark");

    test::check(cuts.size() == static_cast<size_t>(astralia::lock_config::kSlashCount) &&
                    std::ranges::is_sorted(cuts, {}, &astralia::lock::SlashCut::start),
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
    test::check(cuts[astralia::lock_config::kStarCount].a.y >
                        cuts[astralia::lock_config::kStarCount].b.y &&
                    std::fabs(cuts[astralia::lock_config::kStarCount].a.x -
                              cuts[astralia::lock_config::kStarCount].b.x) < w * 0.1f,
                "the flurry opens with a stroke straight up");

    std::vector<astralia::ui::Point> core;
    for (int i = 0; i < astralia::lock_config::kStarCount; ++i) {
        const astralia::lock::SlashCut &p = cuts[i];
        const astralia::lock::SlashCut &q = cuts[(i + 2) % astralia::lock_config::kStarCount];
        float cross = p.direction.x * q.direction.y - p.direction.y * q.direction.x;
        float t = ((q.center.x - p.center.x) * q.direction.y -
                   (q.center.y - p.center.y) * q.direction.x) /
                  cross;
        core.push_back({p.center.x + p.direction.x * t, p.center.y + p.direction.y * t});
    }
    bool clear = true;
    for (size_t k = astralia::lock_config::kStarCount; k < cuts.size(); ++k) {
        const astralia::lock::SlashCut &cut = cuts[k];
        bool below = false;
        bool above = false;
        for (const astralia::ui::Point &p : core) {
            float side =
                cut.direction.x * (p.y - cut.center.y) - cut.direction.y * (p.x - cut.center.x);
            below = below || side < 0.0f;
            above = above || side > 0.0f;
        }
        clear = clear && !(below && above);
    }
    test::check(clear, "no flurry slash cuts through the star's inner pentagon");

    bool spans = true;
    bool near_centre = true;
    std::vector<float> angles;
    for (const astralia::lock::SlashCut &cut : cuts) {
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
        astralia::lock::SlashPlan again(seed, w, h);
        for (size_t i = 0; i < cuts.size(); ++i) {
            same_plan =
                same_plan && again.cuts()[i].a == cuts[i].a && again.cuts()[i].b == cuts[i].b;
        }
    }
    test::check(same_plan, "every seed plays the same slashes");

    astralia::lock::SlashFrame waiting =
        plan.sample(astralia::lock_config::kSlashFinalStart - 0.01f);
    test::check(waiting.desktop.size() == 1 && std::fabs(total_area(waiting) - w * h) < 1.0f,
                "the cutmarks leave the desktop whole");
    test::check(waiting.marks.size() == static_cast<size_t>(astralia::lock_config::kSlashCount) &&
                    waiting.strikes.empty(),
                "during the wait only the cutmarks remain");
    bool thin = std::ranges::all_of(waiting.marks, [](const astralia::lock::SlashMark &mark) {
        return mark.width == astralia::lock_config::kCutmarkWidth &&
               mark.alpha == astralia::lock_config::kCutmarkAlpha;
    });
    test::check(thin, "the cutmarks are thin and steady");
    test::check((astralia::lock_config::kSlashFinalStart - last_land) *
                        astralia::lock_config::kSlashMs >=
                    500.0f,
                "the cutmarks wait a long beat before the final slash");

    astralia::lock::SlashFrame drawing =
        plan.sample(astralia::lock_config::kSlashFinalStart +
                    astralia::lock_config::kSlashFinalDrawSpan * 0.5f);
    test::check(drawing.desktop.size() == 1 &&
                    drawing.marks.size() == astralia::lock_config::kSlashCount &&
                    drawing.strikes.size() == 1 &&
                    drawing.strikes[0].thick == astralia::lock_config::kFinalThick &&
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
                    std::fabs(plan.sample(astralia::lock_config::kSlashFinalStart - 0.01f).dim -
                              astralia::lock_config::kDimPeak) < 0.01f &&
                    plan.sample(land + astralia::lock_config::kSlashDimLift + 0.01f).dim == 0.0f,
                "the desktop dims during the flurry and lifts after the final slash");

    astralia::lock::SlashFrame striking =
        plan.sample(astralia::lock_config::kSlashFirstStart + 0.01f);
    test::check(striking.beams.size() == 1 && striking.beams[0].width > 0.0f &&
                    striking.beams[0].width <= astralia::lock_config::kBeamPeakFrac * h + 0.01f,
                "a strike carries a tapered beam that is thick as it is struck");
    astralia::lock::SlashFrame ending = plan.sample(astralia::lock_config::kSlashFirstStart +
                                                    astralia::lock_config::kSlashDrawSpan - 0.001f);
    test::check(ending.beams.empty() || ending.beams[0].width < striking.beams[0].width,
                "the beam thins out as the strike ends");

    astralia::lock::SlashFrame before = plan.sample(land - 0.001f);
    astralia::lock::SlashFrame after = plan.sample(land + 0.001f);
    test::check(!before.shattered && before.sources.size() == before.desktop.size() &&
                    after.shattered && after.sources.size() == after.desktop.size() &&
                    after.desktop.size() > 1,
                "the desktop breaks into wallpaper shards at the final slash");
    constexpr float tail = astralia::lock_config::kSlashShatterHold +
                           astralia::lock_config::kSlashShatterSpread +
                           astralia::lock_config::kSlashFallSpan;
    test::check(plan.sample(land + tail + 0.001f).desktop.empty() &&
                    !plan.sample(land + tail * 0.5f).desktop.empty() &&
                    tail * astralia::lock_config::kSlashMs < 600.0f,
                "the shards fall away within about half a second");

    astralia::lock::SlashFrame whiteout = plan.sample(land + 0.001f);
    float final_beam = 0.0f;
    for (const astralia::lock::SlashBeam &beam : whiteout.beams) {
        final_beam = std::max(final_beam, beam.width);
    }
    test::check(final_beam > astralia::lock_config::kFinalBeamFrac * h * 0.9f,
                "the final slash lands as a beam about a third of the screen tall");
    test::check(
        !plan.sample(land + astralia::lock_config::kSlashFlashSpan * 0.4f).slivers.empty() &&
            plan.sample(land + astralia::lock_config::kSlashSliverSpan + 0.01f).slivers.empty(),
        "slivers fly off the final slash and are gone after a moment");
    test::check(plan.sample(land + astralia::lock_config::kSlashFlashHold * 0.5f).flash == 1.0f &&
                    plan.sample(land + astralia::lock_config::kSlashFlashSpan * 0.6f).flash <
                        plan.sample(land + astralia::lock_config::kSlashFlashHold + 0.001f).flash,
                "the flash holds for a frame, then decays");

    astralia::lock::SlashFrame impact =
        plan.sample(land + astralia::lock_config::kSlashImpactSpan * 0.3f);
    test::check(impact.flash > 0.0f && !impact.rays.empty() && impact.shock,
                "the final slash lands with the impact flash, rays and shock");
    test::check(impact.desktop.size() >= astralia::lock_config::kSlashCount + 2u &&
                    impact.marks.empty(),
                "the screen shatters along the cutmarks and the final slash");
    test::check(plan.sample(0.02f).flash == 0.0f && plan.sample(land - 0.01f).flash == 0.0f &&
                    plan.sample(land + astralia::lock_config::kSlashFlashSpan + 0.01f).flash ==
                        0.0f &&
                    plan.sample(0.9f).rays.empty() && !plan.sample(0.9f).shock,
                "no flash before the final slash or after its impact");

    bool strikes_valid = true;
    for (int i = 0; i <= 200; ++i) {
        for (const astralia::lock::SlashStrike &strike :
             plan.sample(static_cast<float>(i) / 200.0f).strikes) {
            strikes_valid = strikes_valid && strike.progress >= 0.0f && strike.progress <= 1.0f &&
                            strike.intensity >= 0.0f;
        }
    }
    test::check(strikes_valid, "strikes have a valid progress and intensity");

    test::check(astralia::lock::slash_run_progress(0.0f) == 0.0f &&
                    astralia::lock::slash_run_progress(astralia::lock_config::kSlashMs * 2.0f) ==
                        1.0f,
                "the run goes from 0 to 1 and clamps");

    astralia::lock::SlashPlan empty(1, 0.0f, 0.0f);
    astralia::lock::SlashPlan tiny(1, 1.0f, 1.0f);
    for (int i = 0; i <= 10; ++i) {
        empty.sample(static_cast<float>(i) / 10.0f);
        tiny.sample(static_cast<float>(i) / 10.0f);
    }
    test::check(empty.sample(0.5f).desktop.empty(), "a zero-sized output has no desktop region");

    test::RecordingCanvas canvas;
    auto base = [&] { canvas.rect({0.0f, 0.0f, w, h}, {}); };
    astralia::lock::paint_transition(canvas, plan, 0.0f, base);
    test::check(canvas.ops.size() == 2 && canvas.ops[0].kind == test::Op::Kind::rect &&
                    canvas.ops[1].kind == test::Op::Kind::cut_polygon,
                "progress 0 paints the base and cuts the whole screen");

    int strikes = 0;
    int shocks = 0;
    astralia::lock::Thunder thunder;
    thunder.strike = [&](const astralia::lock::SlashStrike &) { ++strikes; };
    thunder.shock = [&](const astralia::lock::SlashShock &) { ++shocks; };
    canvas.ops.clear();
    astralia::lock::paint_transition(canvas, plan, astralia::lock_config::kSlashFirstStart + 0.005f,
                                     base, thunder);
    test::check(strikes == 1 && shocks == 0 && canvas.count(test::Op::Kind::polygon) >= 4,
                "a strike goes to the thunder hook, beside its beam, cutmark and the dimming");
    canvas.ops.clear();
    strikes = 0;
    astralia::lock::paint_transition(
        canvas, plan, land + astralia::lock_config::kSlashImpactSpan * 0.3f, base, thunder);
    test::check(shocks == 1 && strikes == 1 && canvas.ops[0].kind == test::Op::Kind::rect &&
                    canvas.ops.back().kind == test::Op::Kind::rect &&
                    canvas.count(test::Op::Kind::snapshot_polygon) >=
                        astralia::lock_config::kSlashCount + 2u &&
                    canvas.count(test::Op::Kind::cut_polygon) == 0,
                "the impact frame draws the wallpaper shards, calls the thunder, then flashes");
    canvas.ops.clear();
    astralia::lock::paint_transition(canvas, plan, astralia::lock_config::kSlashFirstStart + 0.005f,
                                     base);
    test::check(canvas.count(test::Op::Kind::polygon) > 1,
                "without a thunder hook the strike falls back to plain strokes");

    canvas.ops.clear();
    astralia::lock::paint_transition(canvas, plan, 0.0f, base, {},
                                     astralia::lock::TransitionMode::unlock);
    test::check(canvas.ops.size() == 1 && canvas.ops[0].kind == test::Op::Kind::snapshot_polygon,
                "unlocking at progress 0 paints the held lock picture and no base");
    canvas.ops.clear();
    astralia::lock::paint_transition(canvas, plan, astralia::lock_config::kSlashFirstStart + 0.005f,
                                     base, {}, astralia::lock::TransitionMode::unlock);
    test::check(canvas.count(test::Op::Kind::snapshot_polygon) == 1 &&
                    canvas.count(test::Op::Kind::cut_polygon) == 0 &&
                    canvas.count(test::Op::Kind::rect) == 0,
                "unlocking cuts the lock picture, never the desktop, and skips the base");
}
