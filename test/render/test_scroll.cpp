#include <cmath>

#include "check.h"
#include "render/recording_canvas.h"

#include "render/scroll.h"

namespace {

using astralia::ScrollAxis;
using astralia::ScrollEntry;
using astralia::ScrollGeometry;
using astralia::ScrollMetrics;
using astralia::ui::Box;

bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

void check_horizontal() {
    using Kind = test::Op::Kind;
    ScrollMetrics metrics;
    Box panel{100, 50, 400, 200};

    ScrollGeometry closed = astralia::scroll_geometry(panel, 0.0f, metrics);
    test::check(closed.paper.w == 0.0f && near(closed.first_roller.x + metrics.roller_w, closed.second_roller.x), "a closed scroll has no paper and touching rollers");
    test::check(near(closed.second_roller.x, panel.x + panel.w / 2.0f), "the closed scroll sits at the panel centre");
    test::check(near(closed.first_roller.h, panel.h + metrics.overhang * 2.0f) && near(closed.first_roller.y, panel.y - metrics.overhang), "rollers overhang the paper");

    ScrollGeometry open = astralia::scroll_geometry(panel, 1.0f, metrics);
    test::check(open.paper == panel, "a fully opened scroll has the panel as its paper");
    test::check(near(open.first_roller.x + metrics.roller_w, panel.x) && near(open.second_roller.x, panel.x + panel.w), "rollers sit on the paper edges");

    float last = -1.0f;
    bool grows = true;
    bool symmetric = true;
    for (int i = 0; i <= 10; ++i) {
        ScrollGeometry g = astralia::scroll_geometry(panel, static_cast<float>(i) / 10.0f, metrics);
        grows = grows && g.paper.w >= last;
        last = g.paper.w;
        symmetric = symmetric && near(g.paper.x + g.paper.w / 2.0f, panel.x + panel.w / 2.0f) && near(g.first_roller.x + metrics.roller_w + g.paper.w, g.second_roller.x);
    }
    test::check(grows, "the paper widens as the scroll opens");
    test::check(symmetric, "the scroll opens evenly about the centre");

    test::check(astralia::scroll_geometry(panel, -1.0f, metrics).paper.w == 0.0f && astralia::scroll_geometry(panel, 2.0f, metrics).paper == panel, "the open value is clamped");

    test::RecordingCanvas shut;
    astralia::paint_scroll(shut, closed, metrics);
    test::RecordingCanvas wide;
    astralia::paint_scroll(wide, open, metrics);
    test::check(shut.count(Kind::rounded) < wide.count(Kind::rounded), "the paper is only drawn once it has width");
}

void check_vertical() {
    using Kind = test::Op::Kind;
    ScrollMetrics metrics;
    metrics.axis = ScrollAxis::vertical;
    metrics.roller_w = 14.0f;
    metrics.overhang = 6.0f;
    Box panel{100, 50, 400, 200};

    ScrollGeometry closed = astralia::scroll_geometry(panel, 0.0f, metrics);
    test::check(closed.paper.h == 0.0f && near(closed.first_roller.y + closed.first_roller.h, closed.second_roller.y), "a closed vertical scroll is one rod");
    test::check(near(closed.first_roller.y, panel.y - metrics.roller_w) && near(closed.first_roller.w, panel.w + metrics.overhang * 2.0f), "the rod spans the card and overhangs it");

    ScrollGeometry open = astralia::scroll_geometry(panel, 1.0f, metrics);
    test::check(open.paper == panel && near(open.second_roller.y, panel.y + panel.h), "an open vertical scroll has the bottom roller under the paper");

    float last = -1.0f;
    bool grows = true;
    bool top_fixed = true;
    for (int i = 0; i <= 10; ++i) {
        ScrollGeometry g = astralia::scroll_geometry(panel, static_cast<float>(i) / 10.0f, metrics);
        grows = grows && g.paper.h >= last;
        last = g.paper.h;
        top_fixed = top_fixed && near(g.first_roller.y, closed.first_roller.y) && near(g.paper.y, panel.y);
    }
    test::check(grows, "the paper grows downward as the scroll opens");
    test::check(top_fixed, "the top roller never moves");

    test::RecordingCanvas shut;
    astralia::paint_scroll(shut, closed, metrics);
    test::RecordingCanvas tall;
    astralia::paint_scroll(tall, open, metrics);
    test::check(shut.count(Kind::rounded) < tall.count(Kind::rounded), "the vertical paper is only drawn once it has height");
}

void check_offset() {
    ScrollMetrics metrics;
    Box panel{300, 100, 400, 200};
    constexpr float sw = 1000.0f;
    constexpr float sh = 800.0f;
    for (ScrollAxis axis : {ScrollAxis::horizontal, ScrollAxis::vertical}) {
        metrics.axis = axis;
        Box closed = astralia::scroll_bounds(astralia::scroll_geometry(panel, 0.0f, metrics));
        for (ScrollEntry entry : {ScrollEntry::from_left, ScrollEntry::from_right, ScrollEntry::from_top, ScrollEntry::from_bottom}) {
            astralia::ScrollOffset none = astralia::scroll_slide_offset(entry, 0.0f, closed, sw, sh);
            test::check(none.dx == 0.0f && none.dy == 0.0f, "a scroll in place has no offset");
            astralia::ScrollOffset out = astralia::scroll_slide_offset(entry, 1.0f, closed, sw, sh);
            Box moved{closed.x + out.dx, closed.y + out.dy, closed.w, closed.h};
            bool gone = moved.x + moved.w <= 0.0f || moved.x >= sw || moved.y + moved.h <= 0.0f || moved.y >= sh;
            test::check(gone, "a scroll that has slid out is off the surface");
            astralia::ScrollOffset half = astralia::scroll_slide_offset(entry, 0.5f, closed, sw, sh);
            test::check(near(half.dx * 2.0f, out.dx) && near(half.dy * 2.0f, out.dy), "the offset is linear in the slide");
        }
    }
    Box closed = astralia::scroll_bounds(astralia::scroll_geometry(panel, 0.0f, ScrollMetrics{}));
    test::check(astralia::scroll_slide_offset(ScrollEntry::from_left, 1.0f, closed, sw, sh).dx < 0.0f && astralia::scroll_slide_offset(ScrollEntry::from_right, 1.0f, closed, sw, sh).dx > 0.0f, "left and right entries move in opposite directions");
    test::check(astralia::scroll_slide_offset(ScrollEntry::from_top, 1.0f, closed, sw, sh).dy < 0.0f && astralia::scroll_slide_offset(ScrollEntry::from_bottom, 1.0f, closed, sw, sh).dy > 0.0f, "top and bottom entries move in opposite directions");
}

} // namespace

void check_scroll() {
    check_horizontal();
    check_vertical();
    check_offset();
}
