#include <cmath>
#include <pango/pangocairo.h>

#include "render/app_fonts.h"

#include "wayland/render/icon.h"

namespace {

// Renders one glyph cropped to its ink box at device pixels, keeping its fractional offset.
RasterizedText rasterize_glyph(const char *family, const cairo_font_options_t *options, const std::string &codepoint_utf8, int32_t scale, int px) {
    RasterizedText result;
    astralia::register_app_fonts();
    scale = scale > 0 ? scale : 1;

    PangoFontDescription *desc = pango_font_description_from_string(family);
    pango_font_description_set_absolute_size(desc, static_cast<double>(px) * scale * PANGO_SCALE);
    // No fallback: a codepoint the font lacks would otherwise draw from another font.
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_fallback_new(FALSE));

    auto layout_on = [&](cairo_t *cr) {
        cairo_set_font_options(cr, options);
        PangoLayout *layout = pango_cairo_create_layout(cr);
        pango_layout_set_font_description(layout, desc);
        pango_layout_set_attributes(layout, attrs);
        pango_layout_set_text(layout, codepoint_utf8.c_str(), -1);
        return layout;
    };

    cairo_surface_t *probe = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *probe_cr = cairo_create(probe);
    PangoLayout *probe_layout = layout_on(probe_cr);
    PangoRectangle ink;
    pango_layout_get_extents(probe_layout, &ink, nullptr);
    g_object_unref(probe_layout);
    cairo_destroy(probe_cr);
    cairo_surface_destroy(probe);

    double ink_x = static_cast<double>(ink.x) / PANGO_SCALE;
    double ink_y = static_cast<double>(ink.y) / PANGO_SCALE;
    int width = static_cast<int>(std::ceil(static_cast<double>(ink.width) / PANGO_SCALE));
    int height = static_cast<int>(std::ceil(static_cast<double>(ink.height) / PANGO_SCALE));
    if (width > 0 && height > 0) {
        cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        cairo_t *cr = cairo_create(surface);
        PangoLayout *layout = layout_on(cr);
        cairo_set_source_rgba(cr, 1, 1, 1, 1);
        cairo_move_to(cr, -ink_x, -ink_y);
        pango_cairo_show_layout(cr, layout);
        cairo_surface_flush(surface);
        result = surface_to_rgba(surface, width, height);
        result.scale = scale;
        g_object_unref(layout);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
    }
    pango_attr_list_unref(attrs);
    pango_font_description_free(desc);
    return result;
}

} // namespace

RasterizedText rasterize_icon(const std::string &codepoint_utf8, int32_t scale, int px) {
    return rasterize_glyph(astralia::icon_font_family, astralia::icon_font_options(), codepoint_utf8, scale, px);
}

RasterizedText rasterize_display_glyph(const std::string &codepoint_utf8, int32_t scale, int px) {
    return rasterize_glyph(astralia::glyph_font_family, astralia_shell_font_options(), codepoint_utf8, scale, px);
}
