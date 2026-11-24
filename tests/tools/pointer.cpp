// tetra-pointer: absolute pointer input for headless scenarios, including
// drags (wlrctl has no press/release).
//
//   tetra-pointer move X Y
//   tetra-pointer click X Y
//   tetra-pointer drag X1 Y1 X2 Y2      press at 1, move in steps, release at 2
//   tetra-pointer hold X1 Y1 X2 Y2      like drag, then keeps holding for 30 s
//   tetra-pointer down X Y / up X Y     move there, then press / release
//
// Coordinates are in pixels of a single output of $TETRA_OUTPUT_SIZE
// (default 1280x720).

#include "wlr-virtual-pointer-unstable-v1-protocol.h"

#include <linux/input-event-codes.h>
#include <wayland-client.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <print>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct Globals {
    wl_seat* seat = nullptr;
    zwlr_virtual_pointer_manager_v1* manager = nullptr;
};

const wl_registry_listener kRegistryListener = {
    .global =
        [](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t /*version*/) {
            auto* g = static_cast<Globals*>(data);
            const std::string_view iface{interface};
            if (iface == wl_seat_interface.name && g->seat == nullptr) {
                g->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
            } else if (iface == zwlr_virtual_pointer_manager_v1_interface.name) {
                g->manager = static_cast<zwlr_virtual_pointer_manager_v1*>(
                    wl_registry_bind(registry, name, &zwlr_virtual_pointer_manager_v1_interface, 1));
            }
        },
    .global_remove = [](void* /*data*/, wl_registry* /*registry*/, uint32_t /*name*/) {},
};

struct Size {
    uint32_t width = 1280;
    uint32_t height = 720;
};

Size output_size() {
    Size size;
    const char* env = std::getenv("TETRA_OUTPUT_SIZE"); // NOLINT(concurrency-mt-unsafe)
    if (env == nullptr) {
        return size;
    }
    const std::string_view text{env};
    const auto x = text.find('x');
    if (x != std::string_view::npos) {
        std::from_chars(text.data(), text.data() + x, size.width);
        std::from_chars(text.data() + x + 1, text.data() + text.size(), size.height);
    }
    // Absolute motion spans the whole layout: headless outputs start side
    // by side, left to right.
    if (const char* outputs = std::getenv("WLR_HEADLESS_OUTPUTS")) { // NOLINT(concurrency-mt-unsafe)
        uint32_t count = 1;
        std::from_chars(outputs, outputs + std::strlen(outputs), count);
        size.width *= std::max<uint32_t>(1, count);
    }
    return size;
}

class Pointer {
public:
    Pointer(wl_display* display, zwlr_virtual_pointer_v1* pointer)
        : display_(display), pointer_(pointer), size_(output_size()) {}

    void move(double x, double y) {
        zwlr_virtual_pointer_v1_motion_absolute(pointer_, now(), static_cast<uint32_t>(x),
                                                static_cast<uint32_t>(y), size_.width, size_.height);
        frame();
    }
    void button(bool pressed) {
        zwlr_virtual_pointer_v1_button(pointer_, now(), BTN_LEFT,
                                       pressed ? WL_POINTER_BUTTON_STATE_PRESSED
                                               : WL_POINTER_BUTTON_STATE_RELEASED);
        frame();
    }
    // One mouse wheel notch; positive scrolls down.
    void wheel(int direction) {
        constexpr double kNotch = 15; // wl_pointer axis units per notch, as libinput sends
        zwlr_virtual_pointer_v1_axis_source(pointer_, WL_POINTER_AXIS_SOURCE_WHEEL);
        zwlr_virtual_pointer_v1_axis_discrete(pointer_, now(), WL_POINTER_AXIS_VERTICAL_SCROLL,
                                              wl_fixed_from_double(kNotch * direction), direction);
        frame();
    }

private:
    void frame() {
        zwlr_virtual_pointer_v1_frame(pointer_);
        wl_display_roundtrip(display_);
        // Leave a frame or so between events, like a hand would.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // Milliseconds on the monotonic clock, as real input devices report, so
    // events from separate runs compare (double clicks).
    [[nodiscard]] static uint32_t now() {
        return static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

    wl_display* display_;
    zwlr_virtual_pointer_v1* pointer_;
    Size size_;
};

std::vector<double> numbers(std::span<char*> args) {
    std::vector<double> out;
    for (char* arg : args) {
        out.push_back(std::atof(arg));
    }
    return out;
}

} // namespace

int main(int argc, char* argv[]) {
    const auto args = std::span(argv, static_cast<std::size_t>(argc)).subspan(1);
    const std::string_view command = args.empty() ? "" : args[0];
    const auto values = numbers(args.empty() ? args : args.subspan(1));
    const bool point = command == "move" || command == "click" || command == "down" || command == "up";
    const bool valid = (point && values.size() == 2) || (command == "scroll" && values.size() == 3) ||
                       ((command == "drag" || command == "hold") && values.size() == 4);
    if (!valid) {
        std::println(stderr, "usage: tetra-pointer move|click|down|up X Y | scroll X Y NOTCHES | "
                             "drag|hold X1 Y1 X2 Y2");
        return 2;
    }

    wl_display* display = wl_display_connect(nullptr);
    if (display == nullptr) {
        return 1;
    }
    Globals globals;
    wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &kRegistryListener, &globals);
    wl_display_roundtrip(display);
    if (globals.manager == nullptr) {
        std::println(stderr, "tetra-pointer: no virtual pointer support");
        return 1;
    }
    zwlr_virtual_pointer_v1* virtual_pointer =
        zwlr_virtual_pointer_manager_v1_create_virtual_pointer(globals.manager, globals.seat);
    Pointer pointer(display, virtual_pointer);

    pointer.move(values[0], values[1]);
    if (command == "down") {
        pointer.button(true);
    } else if (command == "up") {
        pointer.button(false);
    } else if (command == "click") {
        pointer.button(true);
        pointer.button(false);
    } else if (command == "scroll") { // NOTCHES > 0 scrolls down
        const int notches = static_cast<int>(values[2]);
        for (int i = 0; i < std::abs(notches); ++i) {
            pointer.wheel(notches > 0 ? 1 : -1);
        }
    } else if (command == "drag" || command == "hold") {
        pointer.button(true);
        constexpr int kSteps = 12;
        for (int i = 1; i <= kSteps; ++i) {
            const double t = static_cast<double>(i) / kSteps;
            pointer.move(values[0] + ((values[2] - values[0]) * t),
                         values[1] + ((values[3] - values[1]) * t));
        }
        if (command == "drag") {
            pointer.button(false);
        } else {
            // wlroots releases a destroyed device's buttons: stay around
            // (run this in the background) until the session ends.
            std::this_thread::sleep_for(std::chrono::seconds(30));
        }
    }

    zwlr_virtual_pointer_v1_destroy(virtual_pointer);
    zwlr_virtual_pointer_manager_v1_destroy(globals.manager);
    wl_seat_destroy(globals.seat);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return 0;
}
