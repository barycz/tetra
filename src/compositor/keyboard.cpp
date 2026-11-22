#include "keyboard.hpp"

#include "log.hpp"
#include "server.hpp"

#include <memory>
#include <span>

namespace tetra {

namespace {

using XkbContext = std::unique_ptr<xkb_context, decltype(&xkb_context_unref)>;
using XkbKeymap = std::unique_ptr<xkb_keymap, decltype(&xkb_keymap_unref)>;

constexpr int kRepeatRate = 25;    // keys per second
constexpr int kRepeatDelay = 600;  // milliseconds

} // namespace

Keyboard::Keyboard(Server& server, wlr_input_device* device)
    : server_(server), keyboard_(wlr_keyboard_from_input_device(device)) {
    // Layout comes from the XKB_DEFAULT_* environment variables for now.
    const XkbContext context{xkb_context_new(XKB_CONTEXT_NO_FLAGS), xkb_context_unref};
    const XkbKeymap keymap{
        xkb_keymap_new_from_names(context.get(), nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS),
        xkb_keymap_unref};
    if (keymap) {
        wlr_keyboard_set_keymap(keyboard_, keymap.get());
    } else {
        log::error("failed to compile keymap for {}", device->name ? device->name : "keyboard");
    }
    wlr_keyboard_set_repeat_info(keyboard_, kRepeatRate, kRepeatDelay);

    modifiers_.connect(keyboard_->events.modifiers, [this] {
        // One seat, many keyboards: make this one current, then forward.
        wlr_seat_set_keyboard(server_.seat, keyboard_);
        wlr_seat_keyboard_notify_modifiers(server_.seat, &keyboard_->modifiers);
    });
    key_.connect<wlr_keyboard_key_event>(keyboard_->events.key,
                                         [this](auto* event) { handle_key(*event); });
    destroy_.connect(device->events.destroy, [this] { server_.destroy_keyboard(*this); });

    wlr_seat_set_keyboard(server_.seat, keyboard_);
}

void Keyboard::handle_key(const wlr_keyboard_key_event& event) {
    bool handled = false;
    if (event.state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        const uint32_t keycode = event.keycode + 8; // libinput -> xkb
        const xkb_keysym_t* syms = nullptr;
        const int count = xkb_state_key_get_syms(keyboard_->xkb_state, keycode, &syms);
        const uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard_);
        for (const xkb_keysym_t sym : std::span(syms, static_cast<std::size_t>(count))) {
            handled = server_.handle_keybinding(modifiers, sym) || handled;
        }
    }

    if (!handled) {
        wlr_seat_set_keyboard(server_.seat, keyboard_);
        wlr_seat_keyboard_notify_key(server_.seat, event.time_msec, event.keycode, event.state);
    }
}

} // namespace tetra
