#include "keyboard.hpp"

#include "log.hpp"
#include "server.hpp"

#include <memory>
#include <span>

namespace tetra {

namespace {

using XkbContext = std::unique_ptr<xkb_context, decltype(&xkb_context_unref)>;
using XkbKeymap = std::unique_ptr<xkb_keymap, decltype(&xkb_keymap_unref)>;

const char* or_null(const std::string& s) {
    return s.empty() ? nullptr : s.c_str();
}

} // namespace

Keyboard::Keyboard(Server& server, wlr_input_device* device)
    : server_(server), keyboard_(wlr_keyboard_from_input_device(device)),
      is_virtual_(wlr_input_device_get_virtual_keyboard(device) != nullptr) {
    apply_config(server_.config().keyboard);

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

void Keyboard::apply_config(const config::Keyboard& config) {
    if (is_virtual_) {
        return;
    }
    const XkbContext context{xkb_context_new(XKB_CONTEXT_NO_FLAGS), xkb_context_unref};
    const xkb_rule_names names = {
        .rules = nullptr,
        .model = or_null(config.model),
        .layout = or_null(config.layout),
        .variant = or_null(config.variant),
        .options = or_null(config.options),
    };
    XkbKeymap keymap{xkb_keymap_new_from_names(context.get(), &names, XKB_KEYMAP_COMPILE_NO_FLAGS),
                     xkb_keymap_unref};
    if (!keymap) {
        log::error("invalid keyboard layout '{}' (variant '{}'); falling back to 'us'",
                   config.layout, config.variant);
        const xkb_rule_names fallback = {.rules = nullptr, .model = nullptr, .layout = "us",
                                         .variant = nullptr, .options = nullptr};
        keymap.reset(xkb_keymap_new_from_names(context.get(), &fallback, XKB_KEYMAP_COMPILE_NO_FLAGS));
    }
    if (keymap) {
        wlr_keyboard_set_keymap(keyboard_, keymap.get());
    }
    wlr_keyboard_set_repeat_info(keyboard_, config.repeat_rate, config.repeat_delay);
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
