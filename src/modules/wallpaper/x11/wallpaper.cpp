#include <algorithm>
#include <array>
#include <cairo-xcb.h>
#include <chrono>
#include <cstdlib>
#include <malloc.h>
#include <optional>
#include <string>
#include <string_view>

#include "core/log.h"

#include "modules/wallpaper/view.h"
#include "modules/wallpaper/x11/wallpaper.h"

#include "render/tokens.h"
#include "render/wallpaper_paint.h"

#include "service/wallpaper_service.h"

namespace astralia::wallpaper {

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

std::string image_signature(const Config &config, const std::vector<Output> &outputs) {
    std::string signature;
    for (const Output &output : outputs) {
        signature += wallpaper_signature(config, output.name) + "\n";
    }
    return signature;
}

void draw(XConnection &x, xcb_pixmap_t pixmap, const std::vector<Output> &outputs,
          const Config &config) {
    xcb_screen_t *screen = x.screen();
    cairo_surface_t *surface = cairo_xcb_surface_create(
        x.conn(), pixmap, x.visual(), screen->width_in_pixels, screen->height_in_pixels);
    cairo_t *cr = cairo_create(surface);
    cairo_set_source_rgb(cr, palette::base.r, palette::base.g, palette::base.b);
    cairo_paint(cr);
    for (const Output &output : outputs) {
        auto start = std::chrono::steady_clock::now();
        paint_wallpaper(cr, config, output.name, output.geometry.x, output.geometry.y,
                        output.geometry.width, output.geometry.height);
        log::info("wallpaper for {}: {} ms", output.name,
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - start)
                      .count());
    }
    cairo_destroy(cr);
    cairo_surface_finish(surface);
    cairo_surface_destroy(surface);
    malloc_trim(0);
}

} // namespace

Wallpaper::Wallpaper(XConnection &x, EventLoop &loop, Services &services)
    : x_(x), loop_(loop), services_(services),
      menu_window_(x, "astralia-desktop-menu",
                   XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS |
                       XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_LEAVE_WINDOW) {
    loop_.on_window(menu_window_.id(),
                    [this](const xcb_generic_event_t &event) { handle_menu(event); });
    apply(true);
    services_.settings.changed.connect([this] {
        if (image_signature(services_.settings.config(), services_.outputs.outputs()) !=
            applied_signature_) {
            apply(true);
        }
    });
    services_.outputs.changed.connect([this] { apply(false); });
}

Wallpaper::~Wallpaper() {
    close_menu();
    xcb_connection_t *desktop_conn = x_.conn();
    for (xcb_window_t window : desktop_windows_) {
        xcb_destroy_window(desktop_conn, window);
    }
    xcb_flush(desktop_conn);
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
    close_menu();
    layout_desktop_windows();
}

void Wallpaper::layout_desktop_windows() {
    xcb_connection_t *conn = x_.conn();
    for (size_t i = 0; i < applied_.size(); ++i) {
        if (i >= desktop_windows_.size()) {
            xcb_window_t window = xcb_generate_id(conn);
            std::array<uint32_t, 2> values{1u, XCB_EVENT_MASK_BUTTON_PRESS};
            xcb_create_window(conn, XCB_COPY_FROM_PARENT, window, x_.root(), 0, 0, 1, 1, 0,
                              XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT,
                              XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK, values.data());
            xcb_atom_t type = x_.atom("_NET_WM_WINDOW_TYPE_DESKTOP");
            xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window, x_.atom("_NET_WM_WINDOW_TYPE"),
                                XCB_ATOM_ATOM, 32, 1, &type);
            loop_.on_window(
                window, [this, i](const xcb_generic_event_t &event) { handle_desktop(i, event); });
            desktop_windows_.push_back(window);
        }
        const OutputGeometry &geometry = applied_[i].geometry;
        std::array<uint32_t, 5> values{static_cast<uint32_t>(geometry.x),
                                       static_cast<uint32_t>(geometry.y), geometry.width,
                                       geometry.height, XCB_STACK_MODE_BELOW};
        xcb_configure_window(conn, desktop_windows_[i],
                             XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
                                 XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_STACK_MODE,
                             values.data());
        xcb_map_window(conn, desktop_windows_[i]);
    }
    for (size_t i = applied_.size(); i < desktop_windows_.size(); ++i) {
        xcb_unmap_window(conn, desktop_windows_[i]);
    }
    xcb_flush(conn);
}

void Wallpaper::handle_desktop(size_t index, const xcb_generic_event_t &event) {
    if ((event.response_type & ~0x80) != XCB_BUTTON_PRESS || index >= applied_.size()) {
        return;
    }
    const auto &press = reinterpret_cast<const xcb_button_press_event_t &>(event);
    if (press.detail != XCB_BUTTON_INDEX_3 || menu_.is_open()) {
        close_menu();
        return;
    }
    open_menu(index, press.root_x, press.root_y, press.event_x);
}

void Wallpaper::open_menu(size_t index, int16_t root_x, int16_t root_y, int16_t event_x) {
    const Output &output = applied_[index];
    int columns = wallpaper_column_count(services_.settings.config(), output.name, false);
    int column = std::clamp(event_x / std::max(1, output.geometry.width / columns), 0, columns - 1);
    menu_output_ = index;
    menu_.open(static_cast<float>(root_x - output.geometry.x),
               static_cast<float>(root_y - output.geometry.y), output.geometry.width,
               output.geometry.height, column);
    const ui::Box &box = menu_.box();
    OutputGeometry geometry{static_cast<int16_t>(output.geometry.x + static_cast<int>(box.x)),
                            static_cast<int16_t>(output.geometry.y + static_cast<int>(box.y)),
                            static_cast<uint16_t>(box.w), static_cast<uint16_t>(box.h)};
    menu_window_.place(geometry, geometry.width, geometry.height);
    paint_menu();
    menu_window_.show(false);
    menu_window_.present();
    grab_menu();
}

void Wallpaper::close_menu() {
    if (!menu_.is_open()) {
        return;
    }
    menu_.close();
    if (grabbed_) {
        xcb_ungrab_pointer(x_.conn(), XCB_CURRENT_TIME);
        grabbed_ = false;
    }
    menu_window_.set_cursor(CursorShape::arrow);
    menu_window_.hide();
    menu_window_.release();
    xcb_flush(x_.conn());
}

void Wallpaper::grab_menu() {
    constexpr uint16_t mask =
        XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION;
    xcb_grab_pointer_reply_t *reply = xcb_grab_pointer_reply(
        x_.conn(),
        xcb_grab_pointer(x_.conn(), 1, menu_window_.id(), mask, XCB_GRAB_MODE_ASYNC,
                         XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME),
        nullptr);
    grabbed_ = reply != nullptr && reply->status == XCB_GRAB_STATUS_SUCCESS;
    std::free(reply);
}

void Wallpaper::paint_menu() {
    if (!menu_.is_open() || menu_window_.cr() == nullptr) {
        return;
    }
    menu_window_.clear();
    menu_canvas_.bind(menu_window_.cr());
    paint(menu_canvas_, menu_, 0.0f, 0.0f);
    menu_window_.present();
}

void Wallpaper::handle_menu(const xcb_generic_event_t &event) {
    if (!menu_.is_open()) {
        return;
    }
    switch (event.response_type & ~0x80) {
    case XCB_EXPOSE:
        menu_window_.present();
        break;
    case XCB_MOTION_NOTIFY: {
        const auto &motion = reinterpret_cast<const xcb_motion_notify_event_t &>(event);
        if (menu_.hover(motion.event_x, motion.event_y)) {
            menu_window_.set_cursor(menu_.hovered() >= 0 ? CursorShape::hand : CursorShape::arrow);
            paint_menu();
        }
        break;
    }
    case XCB_LEAVE_NOTIFY:
        if (menu_.hovered() >= 0) {
            menu_.clear_hover();
            menu_window_.set_cursor(CursorShape::arrow);
            paint_menu();
        }
        break;
    case XCB_BUTTON_PRESS: {
        const auto &press = reinterpret_cast<const xcb_button_press_event_t &>(event);
        int entry =
            press.detail == XCB_BUTTON_INDEX_1 ? menu_.entry_at(press.event_x, press.event_y) : -1;
        int column = menu_.column();
        std::string output =
            menu_output_ < applied_.size() ? applied_[menu_output_].name : std::string();
        close_menu();
        if (entry < 0) {
            break;
        }
        if (DesktopMenuModel::entries()[static_cast<size_t>(entry)].action ==
            DesktopMenuAction::settings) {
            if (on_settings) {
                on_settings();
            }
        } else if (on_wallpaper) {
            on_wallpaper(output, column);
        }
        break;
    }
    default:
        break;
    }
}

} // namespace astralia::wallpaper
