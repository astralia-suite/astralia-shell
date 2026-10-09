#include <algorithm>

#include "config/logout_config.h"

#include "modules/logout/layout.h"
#include "modules/logout/view.h"

#include "ui/tokens.h"

namespace astralia {

namespace {

namespace cfg = logout_config;

constexpr ui::TextStyle glyph_style{ui::FontFamily::glyph, cfg::glyph_px};

} // namespace

void paint_logout(ui::Canvas &canvas, const LogoutModel &model, float width, float height, const LogoutLogoPainter &logo) {
    if (!model.open_state()) {
        return;
    }
    Point center{width / 2.0, height / 2.0};
    float cx = static_cast<float>(center.x);
    float cy = static_cast<float>(center.y);
    constexpr float size = cfg::button_size;

    for (int i = 0; i < LogoutModel::count; ++i) {
        auto idx = static_cast<size_t>(i);
        float t = model.travel()[idx];
        float visible = model.exiting() ? model.exit_fade() : std::clamp(t, 0.0f, 1.0f);
        if (visible <= 0.002f) {
            continue;
        }
        Point final_center = logout_button_center(i, center);
        float fx = static_cast<float>(final_center.x);
        float fy = static_cast<float>(final_center.y);
        float bx = cx + (fx - cx) * t;
        float by = cy + (fy - cy) * t;

        float highlight = 1.0f + (static_cast<float>(cfg::highlight_scale) - 1.0f) * model.highlight_scale()[idx];
        float scale = (model.exiting() ? 1.0f : t) * highlight;
        float w = size * scale;

        Color fill = with_alpha(palette::field_bg, visible);
        Color border = with_alpha(lerp_color(palette::accent, palette::accent_alt, model.highlight_border()[idx]), visible);
        canvas.rounded({bx - w / 2.0f, by - w / 2.0f, w, w}, static_cast<float>(cfg::button_corner_radius) * scale, fill, cfg::border_width, border);

        const char *glyph = cfg::actions[idx].glyph;
        ui::TextSize extent = canvas.measure(glyph, glyph_style);
        canvas.text(glyph, glyph_style, bx - extent.w / 2.0f, by - extent.h / 2.0f, with_alpha(palette::text, visible));
    }

    constexpr float logo_size = cfg::logo_size;
    canvas.begin_group({cx - logo_size / 2.0f, cy - logo_size / 2.0f, logo_size, logo_size}, {model.logo_scale(), false});
    if (logo) {
        logo(canvas, {0.0f, 0.0f, logo_size, logo_size}, model.exiting() ? model.exit_fade() : 1.0f);
    }
    canvas.end_group();
}

} // namespace astralia
