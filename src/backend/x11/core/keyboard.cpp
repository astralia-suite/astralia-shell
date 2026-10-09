#include <array>
#include <xkbcommon/xkbcommon-x11.h>

#include "core/keyboard.h"
#include "core/log.h"

namespace astralia {

Keyboard::Keyboard(xcb_connection_t *conn)
    : conn_(conn), context_(xkb_context_new(XKB_CONTEXT_NO_FLAGS)) {
    if (xkb_x11_setup_xkb_extension(
            conn_, XKB_X11_MIN_MAJOR_XKB_VERSION, XKB_X11_MIN_MINOR_XKB_VERSION,
            XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS, nullptr, nullptr, nullptr, nullptr) == 0) {
        log::error("keyboard: the X server lacks the XKB extension");
        return;
    }
    device_ = xkb_x11_get_core_keyboard_device_id(conn_);
}

bool Keyboard::reload() {
    if (!context_ || device_ < 0) {
        return false;
    }
    std::unique_ptr<xkb_keymap, Free> keymap(xkb_x11_keymap_new_from_device(
        context_.get(), conn_, device_, XKB_KEYMAP_COMPILE_NO_FLAGS));
    if (!keymap) {
        log::error("keyboard: cannot load the keymap");
        return false;
    }
    std::unique_ptr<xkb_state, Free> state(
        xkb_x11_state_new_from_device(keymap.get(), conn_, device_));
    if (!state) {
        return false;
    }
    keymap_ = std::move(keymap);
    state_ = std::move(state);
    return true;
}

KeyEvent Keyboard::press(xcb_keycode_t keycode, uint16_t modifiers) {
    if (!state_) {
        return {};
    }
    xkb_state_update_mask(state_.get(), modifiers & 0xFF, 0, 0, 0, 0, (modifiers >> 13) & 0x3);
    KeyEvent event;
    event.shift = xkb_state_mod_name_is_active(state_.get(), XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0;
    event.alt = xkb_state_mod_name_is_active(state_.get(), XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0;
    event.ctrl = xkb_state_mod_name_is_active(state_.get(), XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
    switch (xkb_state_key_get_one_sym(state_.get(), keycode)) {
    case XKB_KEY_BackSpace:
        event.kind = KeyKind::backspace;
        break;
    case XKB_KEY_Up:
    case XKB_KEY_KP_Up:
        event.kind = KeyKind::up;
        break;
    case XKB_KEY_Down:
    case XKB_KEY_KP_Down:
        event.kind = KeyKind::down;
        break;
    case XKB_KEY_Left:
    case XKB_KEY_KP_Left:
        event.kind = KeyKind::left;
        break;
    case XKB_KEY_Right:
    case XKB_KEY_KP_Right:
        event.kind = KeyKind::right;
        break;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        event.kind = KeyKind::enter;
        break;
    case XKB_KEY_Escape:
        event.kind = KeyKind::escape;
        break;
    case XKB_KEY_Tab:
        event.kind = KeyKind::tab;
        break;
    default: {
        std::array<char, 64> buf{};
        int size = xkb_state_key_get_utf8(state_.get(), keycode, buf.data(), buf.size());
        auto first = static_cast<unsigned char>(buf[0]);
        if (size > 0 && first >= 0x20 && first != 0x7F) {
            event.kind = KeyKind::text;
            event.text.assign(buf.data(), static_cast<std::size_t>(size));
            const xkb_keysym_t *base_syms = nullptr;
            int base_count = xkb_keymap_key_get_syms_by_level(keymap_.get(), keycode, xkb_state_key_get_layout(state_.get(), keycode), 0, &base_syms);
            event.base_sym = base_count > 0 ? base_syms[0] : XKB_KEY_NoSymbol;
        }
        break;
    }
    }
    return event;
}

} // namespace astralia
