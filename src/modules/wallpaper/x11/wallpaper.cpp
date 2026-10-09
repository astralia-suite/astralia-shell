#include <cairo-xcb.h>
#include <chrono>
#include <cstdlib>
#include <malloc.h>
#include <optional>
#include <string>
#include <string_view>

#include "core/log.h"

#include "modules/wallpaper/x11/wallpaper.h"

#include "service/wallpaper_service.h"

#include "render/cover_cache.h"
#include "render/tokens.h"

namespace astralia {

namespace {

std::optional<xcb_pixmap_t> root_pixmap(XConnection &x, std::string_view atom) {
    auto cookie = xcb_get_property(x.conn(), 0, x.root(), x.atom(atom), XCB_ATOM_PIXMAP, 0, 1);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(x.conn(), cookie, nullptr);
    std::optional<xcb_pixmap_t> pixmap;
    if (reply != nullptr && xcb_get_property_value_length(reply) == sizeof(xcb_pixmap_t)) {
        pixmap = *static_cast<xcb_pixmap_t *>(xcb_get_property_value(reply));
    }
    std::free(reply);
    return pixmap;
}

void set_root_pixmap(XConnection &x, std::string_view atom, xcb_pixmap_t pixmap) {
    xcb_change_property(x.conn(), XCB_PROP_MODE_REPLACE, x.root(), x.atom(atom), XCB_ATOM_PIXMAP,
                        32, 1, &pixmap);
}

void paint_output(cairo_t *cr, const Output &output, const Config &config) {
    std::string configured = wallpaper_image_for(config, output.name);
    std::optional<std::string> path;
    if (!configured.empty()) {
        path = std::move(configured);
    }
    if (!path) {
        log::info("no wallpaper for output {}", output.name);
        return;
    }
    const OutputGeometry &area = output.geometry;
    auto start = std::chrono::steady_clock::now();
    auto image = load_cover(*path, area.width, area.height);
    if (!image) {
        log::error("wallpaper {} for {}: {}", *path, output.name, image.error());
        return;
    }
    auto loaded = std::chrono::steady_clock::now();
    cairo_set_source_surface(cr, image->get(), area.x, area.y);
    cairo_paint(cr);
    auto painted = std::chrono::steady_clock::now();
    log::info("wallpaper {} for {}: load {} ms, paint {} ms", *path, output.name,
              std::chrono::duration_cast<std::chrono::milliseconds>(loaded - start).count(),
              std::chrono::duration_cast<std::chrono::milliseconds>(painted - loaded).count());
}

std::string image_signature(const Config &config, const std::vector<Output> &outputs) {
    std::string signature;
    for (const Output &output : outputs) {
        signature += output.name + "=" + wallpaper_image_for(config, output.name) + "\n";
    }
    return signature;
}

void draw(XConnection &x, xcb_pixmap_t pixmap, const std::vector<Output> &outputs, const Config &config) {
    xcb_screen_t *screen = x.screen();
    cairo_surface_t *surface = cairo_xcb_surface_create(
        x.conn(), pixmap, x.visual(), screen->width_in_pixels, screen->height_in_pixels);
    cairo_t *cr = cairo_create(surface);
    cairo_set_source_rgb(cr, palette::base.r, palette::base.g, palette::base.b);
    cairo_paint(cr);
    for (const Output &output : outputs) {
        paint_output(cr, output, config);
    }
    cairo_destroy(cr);
    cairo_surface_finish(surface);
    cairo_surface_destroy(surface);
    malloc_trim(0);
}

} // namespace

Wallpaper::Wallpaper(XConnection &x, Services &services)
    : x_(x), services_(services) {
    apply(true);
    services_.settings.changed.connect([this] {
        if (image_signature(services_.settings.config(), services_.outputs.outputs()) != applied_signature_) {
            apply(true);
        }
    });
    services_.outputs.changed.connect([this] { apply(false); });
}

Wallpaper::~Wallpaper() {
    if (pixmap_ == XCB_NONE) {
        return;
    }
    xcb_connection_t *conn = x_.conn();
    if (root_pixmap(x_, "_XROOTPMAP_ID") == pixmap_) {
        xcb_delete_property(conn, x_.root(), x_.atom("_XROOTPMAP_ID"));
        uint32_t black = x_.screen()->black_pixel;
        xcb_change_window_attributes(conn, x_.root(), XCB_CW_BACK_PIXEL, &black);
        xcb_clear_area(conn, 0, x_.root(), 0, 0, 0, 0);
    }
    xcb_free_pixmap(conn, pixmap_);
    xcb_flush(conn);
}

void Wallpaper::apply(bool force) {
    const std::vector<Output> &outputs = services_.outputs.outputs();
    if (!force && outputs == applied_) {
        return;
    }
    applied_ = outputs;
    xcb_connection_t *conn = x_.conn();
    xcb_screen_t *screen = x_.screen();
    xcb_pixmap_t pixmap = xcb_generate_id(conn);
    xcb_create_pixmap(conn, screen->root_depth, pixmap, x_.root(), screen->width_in_pixels,
                      screen->height_in_pixels);
    draw(x_, pixmap, applied_, services_.settings.config());
    applied_signature_ = image_signature(services_.settings.config(), applied_);

    xcb_change_window_attributes(conn, x_.root(), XCB_CW_BACK_PIXMAP, &pixmap);
    xcb_clear_area(conn, 0, x_.root(), 0, 0, 0, 0);
    set_root_pixmap(x_, "_XROOTPMAP_ID", pixmap);
    xcb_delete_property(conn, x_.root(), x_.atom("ESETROOT_PMAP_ID"));
    if (pixmap_ != XCB_NONE) {
        xcb_free_pixmap(conn, pixmap_);
    }
    pixmap_ = pixmap;
    xcb_flush(conn);
}

} // namespace astralia
