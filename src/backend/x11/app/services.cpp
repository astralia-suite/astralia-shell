#include <cstdlib>
#include <memory>
#include <string>

#include "app/services.h"

namespace astralia {

namespace {

std::string i3_socket_from_root(XConnection &x) {
    auto cookie = xcb_get_property(x.conn(), 0, x.root(), x.atom("I3_SOCKET_PATH"), XCB_GET_PROPERTY_TYPE_ANY, 0, 256);
    std::unique_ptr<xcb_get_property_reply_t, decltype(&std::free)> reply(xcb_get_property_reply(x.conn(), cookie, nullptr), &std::free);
    if (!reply) {
        return {};
    }
    return {static_cast<const char *>(xcb_get_property_value(reply.get())), static_cast<std::size_t>(xcb_get_property_value_length(reply.get()))};
}

} // namespace

Services::Services(XConnection &x, EventLoop &loop)
    : system(loop), session(loop, BusKind::session), compositor(make_compositor(loop, i3_socket_from_root(x))),
      network(system, loop), bluetooth(system), battery(system), brightness(loop), notifications(loop),
      polkit(loop), audio(loop), media(session), tray(loop), outputs(x, loop), settings(loop) {}

} // namespace astralia
