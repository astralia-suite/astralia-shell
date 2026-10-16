#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>

struct xcb_cursor_context_t;

namespace astralia {

struct OutputGeometry {
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;

    bool operator==(const OutputGeometry &) const = default;
};

struct Output {
    std::string name;
    OutputGeometry geometry;

    bool operator==(const Output &) const = default;
};

enum class CursorShape { arrow, hand };

class XConnection {
  public:
    static std::expected<XConnection, std::string> connect();

    xcb_connection_t *conn() const { return conn_.get(); }
    xcb_screen_t *screen() const { return screen_; }
    xcb_window_t root() const { return screen_->root; }
    xcb_visualtype_t *visual() const { return visual_; }
    xcb_visualtype_t *argb_visual() const { return argb_visual_; }
    xcb_ewmh_connection_t *ewmh() const { return ewmh_.get(); }
    xcb_atom_t atom(std::string_view name);
    xcb_cursor_t cursor(CursorShape shape);
    OutputGeometry primary_output() const;
    std::vector<Output> outputs() const;
    OutputGeometry output_containing(int x, int y) const;
    OutputGeometry pointer_output() const;
    std::pair<int, int> pointer_position() const;

  private:
    struct Disconnect {
        void operator()(xcb_connection_t *conn) const;
    };
    struct WipeEwmh {
        void operator()(xcb_ewmh_connection_t *ewmh) const;
    };
    struct FreeCursorContext {
        void operator()(xcb_cursor_context_t *context) const;
    };
    using ConnPtr = std::unique_ptr<xcb_connection_t, Disconnect>;
    using EwmhPtr = std::unique_ptr<xcb_ewmh_connection_t, WipeEwmh>;
    using CursorContextPtr = std::unique_ptr<xcb_cursor_context_t, FreeCursorContext>;

    XConnection(ConnPtr conn, EwmhPtr ewmh, xcb_screen_t *screen, xcb_visualtype_t *visual);

    bool has_randr() const;

    ConnPtr conn_;
    EwmhPtr ewmh_;
    xcb_screen_t *screen_;
    xcb_visualtype_t *visual_;
    xcb_visualtype_t *argb_visual_;
    std::unordered_map<std::string, xcb_atom_t> atoms_;
    CursorContextPtr cursor_context_;
    std::array<xcb_cursor_t, 2> cursors_{XCB_CURSOR_NONE, XCB_CURSOR_NONE};
};

} // namespace astralia
