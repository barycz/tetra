// tetra-sendkeys: press key chords through the virtual-keyboard protocol, for
// headless tests (wlrctl cannot press F-keys or a lone Super).
//
//   tetra-sendkeys super+F1 super+shift+F2 super Escape
//
// Each argument is one chord: modifiers (super, shift, ctrl, alt) and at most
// one key named as an xkb keysym (F1, Return, a, Escape, ...). A chord of
// only modifiers taps them (e.g. "super" alone opens the launcher).
//
//   tetra-sendkeys --hold 3 shift &
//
// holds each chord down for that many seconds (run it in the background,
// e.g. to keep Shift down while tetra-pointer drags).

#include "virtual-keyboard-unstable-v1-protocol.h"

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>

namespace {

struct Globals {
    wl_seat* seat = nullptr;
    zwp_virtual_keyboard_manager_v1* manager = nullptr;
};

const wl_registry_listener kRegistryListener = {
    .global =
        [](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t /*version*/) {
            auto* globals = static_cast<Globals*>(data);
            const std::string_view iface{interface};
            if (iface == wl_seat_interface.name && globals->seat == nullptr) {
                globals->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
            } else if (iface == zwp_virtual_keyboard_manager_v1_interface.name) {
                globals->manager = static_cast<zwp_virtual_keyboard_manager_v1*>(
                    wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1));
            }
        },
    .global_remove = [](void* /*data*/, wl_registry* /*registry*/, uint32_t /*name*/) {},
};

uint32_t now_ms() {
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// Evdev keycode producing `sym` at level 0 of the first layout, or 0.
uint32_t keycode_for(xkb_keymap* keymap, xkb_keysym_t sym) {
    for (xkb_keycode_t code = xkb_keymap_min_keycode(keymap); code <= xkb_keymap_max_keycode(keymap); ++code) {
        const xkb_keysym_t* syms = nullptr;
        const int count = xkb_keymap_key_get_syms_by_level(keymap, code, 0, 0, &syms);
        for (const xkb_keysym_t s : std::span(syms, static_cast<std::size_t>(std::max(count, 0)))) {
            if (s == sym) {
                return code - 8;
            }
        }
    }
    return 0;
}

struct Modifier {
    const char* name;
    xkb_keysym_t key;
    const char* xkb_mod;
};
constexpr Modifier kModifiers[] = {
    {"super", XKB_KEY_Super_L, XKB_MOD_NAME_LOGO},
    {"shift", XKB_KEY_Shift_L, XKB_MOD_NAME_SHIFT},
    {"ctrl", XKB_KEY_Control_L, XKB_MOD_NAME_CTRL},
    {"alt", XKB_KEY_Alt_L, XKB_MOD_NAME_ALT},
};

} // namespace

int main(int argc, char* argv[]) {
    auto chords = std::span(argv, static_cast<std::size_t>(argc)).subspan(1);
    double hold_seconds = 0;
    if (chords.size() >= 2 && std::string_view(chords[0]) == "--hold") {
        hold_seconds = std::atof(chords[1]);
        chords = chords.subspan(2);
    }
    if (chords.empty()) {
        std::println(stderr, "usage: tetra-sendkeys [--hold SECONDS] CHORD...   e.g. super+F1 super+shift+Return super");
        return 2;
    }

    wl_display* display = wl_display_connect(nullptr);
    if (display == nullptr) {
        std::println(stderr, "tetra-sendkeys: cannot connect to Wayland");
        return 1;
    }
    Globals globals;
    wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &kRegistryListener, &globals);
    wl_display_roundtrip(display);
    if (globals.seat == nullptr || globals.manager == nullptr) {
        std::println(stderr, "tetra-sendkeys: compositor lacks virtual-keyboard support");
        return 1;
    }

    // A plain US keymap, shared with the compositor through a memfd.
    xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    const xkb_rule_names names{.rules = nullptr, .model = nullptr, .layout = "us", .variant = nullptr, .options = nullptr};
    xkb_keymap* keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    char* text = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    const std::size_t size = std::strlen(text) + 1;
    const int fd = memfd_create("tetra-sendkeys-keymap", MFD_CLOEXEC);
    if (fd < 0 || write(fd, text, size) != static_cast<ssize_t>(size)) {
        std::println(stderr, "tetra-sendkeys: cannot share the keymap");
        return 1;
    }
    std::free(text); // NOLINT

    zwp_virtual_keyboard_v1* keyboard =
        zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(globals.manager, globals.seat);
    zwp_virtual_keyboard_v1_keymap(keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, static_cast<uint32_t>(size));
    wl_display_roundtrip(display);
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // let clients get the keymap

    auto send_key = [&](uint32_t code, bool pressed) {
        zwp_virtual_keyboard_v1_key(keyboard, now_ms(), code,
                                    pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
    };

    int status = 0;
    for (const std::string_view chord : chords) {
        std::vector<const Modifier*> mods;
        xkb_keysym_t key = XKB_KEY_NoSymbol;
        std::size_t start = 0;
        while (start <= chord.size()) {
            const auto end = chord.find('+', start);
            const std::string part{chord.substr(start, end == std::string_view::npos ? end : end - start)};
            bool is_mod = false;
            for (const auto& mod : kModifiers) {
                if (part == mod.name) {
                    mods.push_back(&mod);
                    is_mod = true;
                }
            }
            if (!is_mod) {
                key = xkb_keysym_from_name(part.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
                if (key == XKB_KEY_NoSymbol) {
                    std::println(stderr, "tetra-sendkeys: unknown key '{}'", part);
                    status = 2;
                }
            }
            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1;
        }

        uint32_t mask = 0;
        for (const Modifier* mod : mods) {
            send_key(keycode_for(keymap, mod->key), true);
            mask |= 1U << xkb_keymap_mod_get_index(keymap, mod->xkb_mod);
            zwp_virtual_keyboard_v1_modifiers(keyboard, mask, 0, 0, 0);
        }
        if (key != XKB_KEY_NoSymbol) {
            send_key(keycode_for(keymap, key), true);
        }
        if (hold_seconds > 0) {
            wl_display_roundtrip(display);
            std::this_thread::sleep_for(std::chrono::duration<double>(hold_seconds));
        }
        if (key != XKB_KEY_NoSymbol) {
            send_key(keycode_for(keymap, key), false);
        }
        for (auto it = mods.rbegin(); it != mods.rend(); ++it) {
            send_key(keycode_for(keymap, (*it)->key), false);
            mask &= ~(1U << xkb_keymap_mod_get_index(keymap, (*it)->xkb_mod));
            zwp_virtual_keyboard_v1_modifiers(keyboard, mask, 0, 0, 0);
        }
        wl_display_roundtrip(display);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    zwp_virtual_keyboard_v1_destroy(keyboard);
    zwp_virtual_keyboard_manager_v1_destroy(globals.manager);
    wl_seat_destroy(globals.seat);
    wl_registry_destroy(registry);
    wl_display_roundtrip(display);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    close(fd);
    wl_display_disconnect(display);
    return status;
}
