// tetra-pointer-storm: moves the pointer in a circle with absolute motion
// events at a fixed rate, like a VM's tablet device or a high-rate mouse.
//
//   tetra-pointer-storm RATE_HZ SECONDS CX CY RADIUS   (output 1280x720 assumed)

#include "wlr-virtual-pointer-unstable-v1-protocol.h"

#include <wayland-client.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <print>
#include <span>
#include <string_view>
#include <thread>

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

} // namespace

int main(int argc, char* argv[]) {
    const auto args = std::span(argv, static_cast<std::size_t>(argc)).subspan(1);
    if (args.size() != 5) {
        std::println(stderr, "usage: tetra-pointer-storm RATE_HZ SECONDS CX CY RADIUS");
        return 2;
    }
    const double rate = std::atof(args[0]);
    const double seconds = std::atof(args[1]);
    const double cx = std::atof(args[2]);
    const double cy = std::atof(args[3]);
    const double radius = std::atof(args[4]);
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;

    wl_display* display = wl_display_connect(nullptr);
    if (display == nullptr) {
        return 1;
    }
    Globals globals;
    wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &kRegistryListener, &globals);
    wl_display_roundtrip(display);
    if (globals.manager == nullptr) {
        std::println(stderr, "tetra-pointer-storm: no virtual pointer support");
        return 1;
    }
    zwlr_virtual_pointer_v1* pointer =
        zwlr_virtual_pointer_manager_v1_create_virtual_pointer(globals.manager, globals.seat);

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    const auto period = std::chrono::duration<double>(1.0 / rate);
    long events = 0;
    for (auto next = start; clock::now() - start < std::chrono::duration<double>(seconds);
         next += std::chrono::duration_cast<clock::duration>(period)) {
        const double t = std::chrono::duration<double>(clock::now() - start).count();
        const double angle = t * 2 * std::numbers::pi; // one turn per second
        const auto x = static_cast<uint32_t>(cx + (radius * std::cos(angle)));
        const auto y = static_cast<uint32_t>(cy + (radius * std::sin(angle)));
        const auto ms = static_cast<uint32_t>(t * 1000);
        zwlr_virtual_pointer_v1_motion_absolute(pointer, ms, x, y, kWidth, kHeight);
        zwlr_virtual_pointer_v1_frame(pointer);
        wl_display_flush(display);
        ++events;
        std::this_thread::sleep_until(next);
    }
    wl_display_roundtrip(display);
    std::println(stderr, "tetra-pointer-storm: {} events in {:.1f} s", events, seconds);

    zwlr_virtual_pointer_v1_destroy(pointer);
    zwlr_virtual_pointer_manager_v1_destroy(globals.manager);
    wl_seat_destroy(globals.seat);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return 0;
}
