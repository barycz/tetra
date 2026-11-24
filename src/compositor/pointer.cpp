#include "pointer.hpp"

#include "log.hpp"
#include "server.hpp"

#include <algorithm>

namespace tetra {

namespace {

// Logs settings a device refuses (e.g. a value out of its range).
void check(libinput_config_status status, const char* what, const wlr_input_device* device) {
    if (status != LIBINPUT_CONFIG_STATUS_SUCCESS) {
        log::error("input '{}': cannot set {}: {}", device->name != nullptr ? device->name : "?", what,
                   libinput_config_status_to_str(status));
    }
}

libinput_config_scroll_method scroll_method(config::Touchpad::Scroll scroll) {
    switch (scroll) {
    case config::Touchpad::Scroll::TwoFinger:
        return LIBINPUT_CONFIG_SCROLL_2FG;
    case config::Touchpad::Scroll::Edge:
        return LIBINPUT_CONFIG_SCROLL_EDGE;
    case config::Touchpad::Scroll::None:
        break;
    }
    return LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
}

} // namespace

Pointer::Pointer(Server& server, wlr_input_device* device) : server_(server), device_(device) {
    if (wlr_input_device_is_libinput(device)) {
        libinput_ = wlr_libinput_get_device_handle(device);
        // Touchpads are the pointers that can tap; mice, trackpoints and
        // trackballs cannot.
        touchpad_ = libinput_device_config_tap_get_finger_count(libinput_) > 0;
        log::info("input '{}': {}", device->name != nullptr ? device->name : "?",
                  touchpad_ ? "touchpad" : "mouse");
        apply_config(server_.config().mouse, server_.config().touchpad);
    }
    destroy_.connect(device->events.destroy, [this] { server_.destroy_pointer(*this); });
}

void Pointer::apply_config(const config::Mouse& mouse, const config::Touchpad& touchpad) {
    if (libinput_ == nullptr) {
        return;
    }
    libinput_device* d = libinput_;

    if (libinput_device_config_accel_is_available(d) != 0) {
        const double speed = std::clamp(touchpad_ ? touchpad.speed : mouse.speed, -1.0, 1.0);
        check(libinput_device_config_accel_set_speed(d, speed), "speed", device_);
        if (!touchpad_) {
            const auto profile =
                mouse.acceleration ? LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE : LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
            if ((libinput_device_config_accel_get_profiles(d) & profile) != 0) {
                check(libinput_device_config_accel_set_profile(d, profile), "acceleration", device_);
            }
        }
    }
    if (libinput_device_config_scroll_has_natural_scroll(d) != 0) {
        const bool natural = touchpad_ ? touchpad.natural_scroll : mouse.natural_scroll;
        check(libinput_device_config_scroll_set_natural_scroll_enabled(d, natural ? 1 : 0), "natural scrolling",
              device_);
    }
    if (libinput_device_config_left_handed_is_available(d) != 0) {
        check(libinput_device_config_left_handed_set(d, mouse.left_handed ? 1 : 0), "left-handed", device_);
    }
    if (!touchpad_) {
        return;
    }

    check(libinput_device_config_tap_set_enabled(
              d, touchpad.tap_to_click ? LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED),
          "tap to click", device_);
    if (libinput_device_config_dwt_is_available(d) != 0) {
        check(libinput_device_config_dwt_set_enabled(d, touchpad.disable_while_typing ? LIBINPUT_CONFIG_DWT_ENABLED
                                                                                       : LIBINPUT_CONFIG_DWT_DISABLED),
              "disable while typing", device_);
    }
    const auto scroll = scroll_method(touchpad.scroll);
    if (scroll == LIBINPUT_CONFIG_SCROLL_NO_SCROLL ||
        (libinput_device_config_scroll_get_methods(d) & static_cast<std::uint32_t>(scroll)) != 0) {
        check(libinput_device_config_scroll_set_method(d, scroll), "scrolling", device_);
    }
    // Without a setting, go back to the device's own default (it may have
    // been changed by an earlier configuration).
    auto click = libinput_device_config_click_get_default_method(d);
    if (touchpad.click) {
        click = *touchpad.click == config::Touchpad::Click::Fingers ? LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER
                                                                    : LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
    }
    if ((libinput_device_config_click_get_methods(d) & static_cast<std::uint32_t>(click)) != 0) {
        check(libinput_device_config_click_set_method(d, click), "click method", device_);
    }
}

} // namespace tetra
