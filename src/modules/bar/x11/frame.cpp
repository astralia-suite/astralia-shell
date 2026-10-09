#include <numbers>

#include "modules/bar/x11/frame.h"

#include "render/draw.h"

namespace astralia {

namespace {

void island_path(cairo_t *cr, double x, double width, double height, double radius) {
    rounded_rect(cr, x, -radius, width, height + radius, radius);
}

void fillet_path(cairo_t *cr, double x, double y, double size, double center_x, double center_y, double radius) {
    cairo_rectangle(cr, x, y, size, size);
    cairo_clip(cr);
    cairo_new_path(cr);
    cairo_rectangle(cr, x, y, size, size);
    cairo_arc(cr, center_x, center_y, radius, 0.0, 2.0 * std::numbers::pi);
}

void fillet_outer(cairo_t *cr, const BarStyleSpec &spec, const Fillet &fillet) {
    double radius = spec.fillet_radius;
    double x = fillet.right_of_island ? fillet.edge_x : fillet.edge_x - radius;
    double center_x = fillet.right_of_island ? fillet.edge_x + radius : fillet.edge_x - radius;
    cairo_save(cr);
    fillet_path(cr, x, spec.rail_height, radius, center_x, spec.rail_height + radius, radius);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_fill(cr);
    cairo_restore(cr);
}

void fillet_inner(cairo_t *cr, const BarStyleSpec &spec, const Fillet &fillet) {
    double border = spec.border_width;
    double radius = spec.fillet_radius;
    double size = radius + border;
    double x = fillet.right_of_island ? fillet.edge_x - border : fillet.edge_x - radius;
    double center_x = fillet.right_of_island ? fillet.edge_x + radius : fillet.edge_x - radius;
    cairo_save(cr);
    fillet_path(cr, x, spec.rail_height - border, size, center_x, spec.rail_height + radius, size);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_fill(cr);
    cairo_restore(cr);
}

void paint_continuous(cairo_t *cr, const BarStyleSpec &spec, double width, double height) {
    double border = spec.border_width;
    double inset = border / 2.0;
    double radius = height * spec.radius_ratio;
    set_source(cr, palette::base_alpha80);
    rounded_rect(cr, 0.0, 0.0, width, height, radius);
    cairo_fill(cr);
    set_source(cr, palette::accent);
    cairo_set_line_width(cr, border);
    rounded_rect(cr, inset, inset, width - 2 * inset, height - 2 * inset, radius - inset);
    cairo_stroke(cr);
}

void paint_okinami(cairo_t *cr, const BarStyleSpec &spec, const BarFrame &frame, double width, double height) {
    double border = spec.border_width;
    set_source(cr, palette::accent);
    cairo_rectangle(cr, 0.0, 0.0, width, spec.rail_height);
    cairo_fill(cr);
    for (const IslandShape &island : frame.islands) {
        island_path(cr, island.outer_x, island.outer_width, height, spec.island_radius);
        cairo_fill(cr);
    }
    for (const Fillet &fillet : frame.fillets) {
        fillet_outer(cr, spec, fillet);
    }

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    set_source(cr, palette::base_alpha80);
    cairo_rectangle(cr, 0.0, 0.0, width, spec.rail_height - border);
    cairo_fill(cr);
    for (const IslandShape &island : frame.islands) {
        rounded_rect(cr, island.inner_x, -spec.island_radius, island.inner_width, height + spec.island_radius - border, spec.island_radius - border);
        cairo_fill(cr);
    }
    for (const Fillet &fillet : frame.fillets) {
        fillet_inner(cr, spec, fillet);
    }
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
}

} // namespace

void paint_bar_frame(cairo_t *cr, const BarStyleSpec &spec, const BarFrame &frame, double width, double height) {
    if (spec.has_rail()) {
        paint_okinami(cr, spec, frame, width, height);
    } else {
        paint_continuous(cr, spec, width, height);
    }
}

} // namespace astralia
