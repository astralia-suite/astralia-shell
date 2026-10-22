#include <algorithm>
#include <array>
#include <cmath>

#include "config/lock_config.h"

#include "modules/lock/transition_view.h"

#include "render/tokens.h"

namespace astralia::lock {

namespace {

constexpr Color dim_color = color(lock_config::kDimHex);
constexpr Color flash_color = color(lock_config::kFlashHex);

void stroke(ui::Canvas &canvas, ui::Point a, ui::Point b, float width, const Color &color) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float length = std::hypot(dx, dy);
    if (length < 1e-4f) {
        return;
    }
    float nx = -dy / length * width * 0.5f;
    float ny = dx / length * width * 0.5f;
    std::array<ui::Point, 4> quad{
        {{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}}};
    canvas.polygon(quad, color);
}

void beam(ui::Canvas &canvas, const SlashBeam &beam) {
    float dx = beam.b.x - beam.a.x;
    float dy = beam.b.y - beam.a.y;
    float length = std::hypot(dx, dy);
    if (length < 1e-4f || beam.width <= 0.0f) {
        return;
    }
    ui::Point middle{(beam.a.x + beam.b.x) * 0.5f, (beam.a.y + beam.b.y) * 0.5f};
    float nx = -dy / length;
    float ny = dx / length;
    auto shape = [&](float width) {
        float half = width * 0.5f;
        return std::array<ui::Point, 4>{{beam.a,
                                         {middle.x + nx * half, middle.y + ny * half},
                                         beam.b,
                                         {middle.x - nx * half, middle.y - ny * half}}};
    };
    canvas.polygon(shape(beam.width * lock_config::kBeamGlowScale),
                   with_alpha(palette::electro, lock_config::kBeamGlowAlpha * beam.alpha));
    canvas.polygon(shape(beam.width), with_alpha(flash_color, beam.alpha));
}

void fallback_strike(ui::Canvas &canvas, const SlashStrike &strike) {
    ui::Point tip{strike.a.x + (strike.b.x - strike.a.x) * strike.progress,
                  strike.a.y + (strike.b.y - strike.a.y) * strike.progress};
    float width = lock_config::kFallbackBoltWidth * strike.thick;
    float alpha = std::clamp(strike.intensity, 0.0f, 1.0f);
    stroke(canvas, strike.a, tip, width * lock_config::kBoltGlowFarScale,
           with_alpha(palette::electro, lock_config::kBoltGlowFarAlpha * alpha));
    stroke(canvas, strike.a, tip, width * lock_config::kBoltGlowNearScale,
           with_alpha(palette::electro, lock_config::kBoltGlowNearAlpha * alpha));
    stroke(canvas, strike.a, tip, width, with_alpha(palette::white, alpha));
}

} // namespace

void paint_transition(ui::Canvas &canvas, const SlashPlan &plan, float progress,
                      const std::function<void()> &paint_base, const Thunder &thunder,
                      TransitionMode mode) {
    bool unlock = mode == TransitionMode::unlock;
    if (paint_base && !unlock) {
        paint_base();
    }
    SlashFrame frame = plan.sample(progress);

    for (size_t i = 0; i < frame.desktop.size(); ++i) {
        const SlashPolygon &piece = frame.desktop[i];
        if ((frame.shattered || unlock) && i < frame.sources.size()) {
            canvas.snapshot_polygon(frame.sources[i], piece, 1.0f);
        } else {
            canvas.cut_polygon(piece);
        }
        if (frame.dim > 0.0f) {
            canvas.polygon(piece, with_alpha(dim_color, frame.dim));
        }
    }

    for (const SlashMark &mark : frame.marks) {
        stroke(canvas, mark.a, mark.b, mark.width, with_alpha(flash_color, mark.alpha));
    }

    for (const SlashBeam &slash_beam : frame.beams) {
        beam(canvas, slash_beam);
    }

    for (const SlashPolygon &ray : frame.rays) {
        canvas.polygon(ray, with_alpha(palette::white, lock_config::kSlashRayAlpha * frame.flash));
    }

    for (const SlashStrike &strike : frame.strikes) {
        if (thunder.strike) {
            thunder.strike(strike);
        } else {
            fallback_strike(canvas, strike);
        }
    }
    if (frame.shock && thunder.shock) {
        thunder.shock(*frame.shock);
    }

    for (const SlashPolygon &sliver : frame.slivers) {
        canvas.polygon(sliver,
                       with_alpha(flash_color, lock_config::kSliverAlpha * frame.sliver_alpha));
    }

    if (frame.flash > 0.0f) {
        canvas.rect({0.0f, 0.0f, plan.width(), plan.height()},
                    with_alpha(flash_color, lock_config::kSlashFlashPeak * frame.flash));
    }
}

} // namespace astralia::lock
