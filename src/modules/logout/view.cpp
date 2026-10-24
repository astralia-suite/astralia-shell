#include <algorithm>

#include "config/logout_config.h"

#include "modules/logout/layout.h"
#include "modules/logout/view.h"

#include "render/sticker.h"
#include "render/tokens.h"

namespace astralia::logout {

namespace {

namespace cfg = logout_config;

constexpr ui::TextStyle glyph_style{ui::FontFamily::glyph, cfg::glyph_px};

} // namespace

void paint(ui::Canvas &canvas, const Model &model, float width, float height,
           std::string_view logo_path) {
    if (!model.open_state()) {
        return;
    }
    Point center{width / 2.0, height / 2.0};
    float cx = static_cast<float>(center.x);
    float cy = static_cast<float>(center.y);
    constexpr float size = cfg::button_size;

    if (model.scrim() > 0.0f) {
        canvas.rect({0.0f, 0.0f, width, height}, with_alpha(palette::shadow, model.scrim()));
    }

    for (int i = 0; i < Model::count; ++i) {
        auto idx = static_cast<size_t>(i);
        float t = model.travel()[idx];
        float visible = model.exiting() ? model.exit_fade() : std::clamp(t, 0.0f, 1.0f);
        if (visible <= 0.002f) {
            continue;
        }
        Point final_center = button_center(i, center);
        float fx = static_cast<float>(final_center.x);
        float fy = static_cast<float>(final_center.y);
        float bx = cx + (fx - cx) * t;
        float by = cy + (fy - cy) * t;

        float highlight =
            1.0f + (static_cast<float>(cfg::highlight_scale) - 1.0f) * model.highlight_scale()[idx];
        float scale = (model.exiting() ? 1.0f : t) * highlight;
        float w = size * scale;

        Color fill = with_alpha(
            lerp_color(palette::paper, palette::accent_alt, model.highlight_border()[idx]),
            visible);
        sticker(canvas, {bx - w / 2.0f, by - w / 2.0f, w, w}, metrics::radius_card * scale, fill,
                with_alpha(palette::ink, visible), metrics::border_thick,
                metrics::shadow_offset * scale);

        const char *glyph = cfg::actions[idx].glyph;
        ui::TextSize extent = canvas.measure(glyph, glyph_style);
        canvas.text(glyph, glyph_style, bx - extent.w / 2.0f, by - extent.h / 2.0f,
                    with_alpha(palette::text, visible));
    }

    constexpr float logo_size = cfg::logo_size;
    canvas.begin_group({cx - logo_size / 2.0f, cy - logo_size / 2.0f, logo_size, logo_size},
                       {model.logo_scale(), false});
    ui::ImageId logo = canvas.image(
        logo_path.empty() ? std::string_view(cfg::logo_asset) : logo_path, cfg::logo_size);
    if (logo != ui::no_image) {
        ui::TextSize extent = canvas.image_size(logo);
        float fit = std::min(logo_size / extent.w, logo_size / extent.h);
        float w = extent.w * fit;
        float h = extent.h * fit;
        float alpha = model.exiting() ? model.exit_fade() : 1.0f;
        canvas.draw_image(logo, {(logo_size - w) / 2.0f, (logo_size - h) / 2.0f, w, h},
                          with_alpha(palette::white, alpha));
    }
    canvas.end_group();

    if (model.flash() > 0.0f) {
        canvas.rect({0.0f, 0.0f, width, height},
                    with_alpha(palette::accent_container, model.flash()));
    }
}

} // namespace astralia::logout
