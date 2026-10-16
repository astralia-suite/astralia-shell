#include <array>
#include <cstdlib>
#include <memory>
#include <utility>

#include "core/x_clipboard.h"

namespace astralia {

namespace {

struct FreeReply {
    void operator()(void *reply) const { std::free(reply); }
};

template <typename T> using Reply = std::unique_ptr<T, FreeReply>;

constexpr uint32_t max_paste_words = 1 << 18;

} // namespace

X11Clipboard::X11Clipboard(XConnection &x, xcb_window_t window) : x_(x), window_(window) {}

void X11Clipboard::set(std::string text) {
    text_ = std::move(text);
    xcb_set_selection_owner(x_.conn(), window_, x_.atom("CLIPBOARD"), XCB_CURRENT_TIME);
    xcb_flush(x_.conn());
    owner_ = true;
}

void X11Clipboard::request(std::function<void(std::string)> done) {
    if (owner_) {
        done(text_);
        return;
    }
    pending_ = std::move(done);
    xcb_atom_t property = x_.atom("ASTRALIA_PASTE");
    xcb_convert_selection(x_.conn(), window_, x_.atom("CLIPBOARD"), x_.atom("UTF8_STRING"),
                          property, XCB_CURRENT_TIME);
    xcb_flush(x_.conn());
}

bool X11Clipboard::handle(const xcb_generic_event_t &event) {
    switch (event.response_type & ~0x80) {
    case XCB_SELECTION_REQUEST:
        answer(reinterpret_cast<const xcb_selection_request_event_t &>(event));
        return true;
    case XCB_SELECTION_CLEAR:
        if (reinterpret_cast<const xcb_selection_clear_event_t &>(event).selection ==
            x_.atom("CLIPBOARD")) {
            owner_ = false;
        }
        return true;
    case XCB_SELECTION_NOTIFY:
        receive(reinterpret_cast<const xcb_selection_notify_event_t &>(event));
        return true;
    default:
        return false;
    }
}

void X11Clipboard::answer(const xcb_selection_request_event_t &request) {
    xcb_connection_t *conn = x_.conn();
    xcb_atom_t property = request.property != XCB_ATOM_NONE ? request.property : request.target;
    xcb_atom_t targets = x_.atom("TARGETS");
    xcb_atom_t utf8 = x_.atom("UTF8_STRING");
    xcb_atom_t mime_utf8 = x_.atom("text/plain;charset=utf-8");
    xcb_atom_t mime_plain = x_.atom("text/plain");
    xcb_atom_t text_atom = x_.atom("TEXT");
    bool answered = false;
    if (request.selection == x_.atom("CLIPBOARD") && owner_) {
        if (request.target == targets) {
            std::array<xcb_atom_t, 6> supported{targets,    utf8,      mime_utf8,
                                                mime_plain, text_atom, XCB_ATOM_STRING};
            xcb_change_property(conn, XCB_PROP_MODE_REPLACE, request.requestor, property,
                                XCB_ATOM_ATOM, 32, static_cast<uint32_t>(supported.size()),
                                supported.data());
            answered = true;
        } else if (request.target == utf8 || request.target == mime_utf8 ||
                   request.target == mime_plain) {
            xcb_change_property(conn, XCB_PROP_MODE_REPLACE, request.requestor, property, utf8, 8,
                                static_cast<uint32_t>(text_.size()), text_.data());
            answered = true;
        } else if (request.target == XCB_ATOM_STRING || request.target == text_atom) {
            xcb_change_property(conn, XCB_PROP_MODE_REPLACE, request.requestor, property,
                                XCB_ATOM_STRING, 8, static_cast<uint32_t>(text_.size()),
                                text_.data());
            answered = true;
        }
    }
    xcb_selection_notify_event_t notify{};
    notify.response_type = XCB_SELECTION_NOTIFY;
    notify.time = request.time;
    notify.requestor = request.requestor;
    notify.selection = request.selection;
    notify.target = request.target;
    notify.property = answered ? property : static_cast<xcb_atom_t>(XCB_ATOM_NONE);
    xcb_send_event(conn, 0, request.requestor, XCB_EVENT_MASK_NO_EVENT,
                   reinterpret_cast<const char *>(&notify));
    xcb_flush(conn);
}

void X11Clipboard::receive(const xcb_selection_notify_event_t &notify) {
    if (notify.requestor != window_ || notify.selection != x_.atom("CLIPBOARD") || !pending_) {
        return;
    }
    std::function<void(std::string)> done = std::move(pending_);
    pending_ = nullptr;
    std::string text;
    if (notify.property != XCB_ATOM_NONE) {
        xcb_get_property_cookie_t cookie = xcb_get_property(
            x_.conn(), 1, window_, notify.property, XCB_GET_PROPERTY_TYPE_ANY, 0, max_paste_words);
        Reply<xcb_get_property_reply_t> reply(xcb_get_property_reply(x_.conn(), cookie, nullptr));
        if (reply && reply->format == 8 && reply->type != x_.atom("INCR")) {
            text.assign(static_cast<const char *>(xcb_get_property_value(reply.get())),
                        static_cast<size_t>(xcb_get_property_value_length(reply.get())));
        }
    }
    done(std::move(text));
}

} // namespace astralia
