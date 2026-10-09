#include <algorithm>
#include <cairo/cairo.h>
#include <cmath>

#include "wayland/render/arc_gauge.h"
#include "wayland/render/text.h"

// segments
constexpr int kArcGaugeSegments = 10;
constexpr float kArcGaugeSegmentGapDeg = 6.0f;

const Texture *cached_arc_gauge(TextureCache &tcache, int32_t scale, float diameter, float stroke, float value01, const astralia::Color &fill_color) {
    int px_diameter = static_cast<int>(diameter * scale);
    int bucket =
        static_cast<int>(std::round(std::clamp(value01, 0.0f, 1.0f) * 100.0f));
    std::string key =
        "arc_gauge:" + std::to_string(px_diameter) + ":" +
        std::to_string(static_cast<int>(stroke * scale)) + ":" +
        std::to_string(bucket) + ":" +
        std::to_string(static_cast<int>(fill_color.r * 255)) + "," +
        std::to_string(static_cast<int>(fill_color.g * 255)) + "," +
        std::to_string(static_cast<int>(fill_color.b * 255));
    return tcache.get(key, [&]() -> RasterizedText {
        cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px_diameter, px_diameter);
        cairo_t *cr = cairo_create(surface);
        float cx = px_diameter / 2.0f, cy = px_diameter / 2.0f;
        float px_stroke = stroke * scale;
        float radius = px_diameter / 2.0f - px_stroke / 2.0f;
        float start = -static_cast<float>(M_PI) / 2.0f;
        float full = 2.0f * static_cast<float>(M_PI);
        float step = full / kArcGaugeSegments;
        float gap = kArcGaugeSegmentGapDeg * static_cast<float>(M_PI) / 180.0f;
        float span = step - gap;

        cairo_set_line_width(cr, px_stroke);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);

        float lit = static_cast<float>(bucket) / 100.0f * kArcGaugeSegments;
        for (int i = 0; i < kArcGaugeSegments; ++i) {
            float seg_start = start + i * step + gap / 2.0f;
            cairo_set_source_rgba(cr, fill_color.r, fill_color.g, fill_color.b, 0.15f);
            cairo_arc(cr, cx, cy, radius, seg_start, seg_start + span);
            cairo_stroke(cr);

            float seg_fill = std::clamp(lit - static_cast<float>(i), 0.0f, 1.0f);
            if (seg_fill > 0.0f) {
                cairo_set_source_rgba(cr, fill_color.r, fill_color.g, fill_color.b, fill_color.a);
                cairo_arc(cr, cx, cy, radius, seg_start, seg_start + span * seg_fill);
                cairo_stroke(cr);
            }
        }

        cairo_surface_flush(surface);
        RasterizedText result =
            surface_to_rgba(surface, px_diameter, px_diameter);
        result.scale = scale;
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return result;
    });
}
