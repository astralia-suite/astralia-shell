#include "modules/bar/paint/frame.h"

#include "modules/bar/paint/chips.h"

namespace astralia::bar {

void frame_base(ui::Canvas &canvas, const StyleSpec &style, const Frame &frame, float width,
                float height) {
    if (style.continuous) {
        paint_ringed(canvas, style, {0.0f, 0.0f, width, height}, style.bg);
        return;
    }
    if (!style.has_rail())
        return;
    float radius = style.island_radius;
    float bw = style.border_width;
    canvas.rect({0.0f, 0.0f, width, style.rail_height}, style.border);
    for (const IslandShape &island : frame.islands)
        canvas.rounded({island.outer_x, -radius, island.outer_width, height + radius}, radius,
                       style.border, 0.0f, style.border);
    canvas.set_erase(true);
    canvas.rect({0.0f, 0.0f, width, style.rail_height - bw}, style.bg);
    for (const IslandShape &island : frame.islands)
        canvas.rounded({island.inner_x, -radius, island.inner_width, height + radius - bw},
                       radius - bw, style.bg, 0.0f, style.bg);
    canvas.set_erase(false);
}

void frame_overlay(ui::Canvas &canvas, const StyleSpec &style, const Frame &frame, float width,
                   float height, float hug_radius, bool left_flush, bool right_flush) {
    if (!style.has_rail())
        return;
    float bw = style.border_width;
    float fr = style.fillet_radius;
    for (const Fillet &fillet : frame.fillets) {
        float x = fillet.right_of_island ? fillet.edge_x : fillet.edge_x - fr;
        canvas.fillet({x, style.rail_height, fr, fr}, fillet.right_of_island, style.border);
    }
    canvas.set_erase(true);
    for (const Fillet &fillet : frame.fillets) {
        float x = fillet.right_of_island ? fillet.edge_x - bw : fillet.edge_x - fr;
        canvas.fillet({x, style.rail_height - bw, fr + bw, fr + bw}, fillet.right_of_island,
                      style.bg);
    }
    canvas.set_erase(false);

    if (hug_radius <= 0.0f)
        return;
    if (left_flush) {
        canvas.fillet({0.0f, height, hug_radius, hug_radius}, true, style.border);
        canvas.set_erase(true);
        canvas.fillet({-bw, height - bw, hug_radius + bw, hug_radius + bw}, true, style.bg);
        canvas.set_erase(false);
    }
    if (right_flush) {
        canvas.fillet({width - hug_radius, height, hug_radius, hug_radius}, false, style.border);
        canvas.set_erase(true);
        canvas.fillet({width - hug_radius, height - bw, hug_radius + bw, hug_radius + bw}, false,
                      style.bg);
        canvas.set_erase(false);
    }
}

} // namespace astralia::bar
