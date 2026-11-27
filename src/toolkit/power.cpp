#include "power.hpp"

#include <print>

namespace tetra::ui {

namespace {

constexpr const char* kManager = "org.freedesktop.login1.Manager";

struct Names {
    const char* can;
    const char* request;
    const char* label;
};

constexpr std::array<Names, Power::kActions> kNames{{
    {"CanSuspend", "Suspend", "Suspend"},
    {"CanReboot", "Reboot", "Restart"},
    {"CanPowerOff", "PowerOff", "Shut down"},
}};

} // namespace

Power::Power(sdbus::IConnection* system_bus) {
    if (system_bus == nullptr) {
        return;
    }
    try {
        manager_ = sdbus::createProxy(*system_bus, sdbus::ServiceName{"org.freedesktop.login1"},
                                      sdbus::ObjectPath{"/org/freedesktop/login1"});
    } catch (const sdbus::Error& e) {
        std::println(stderr, "power actions unavailable: {}", e.what());
    }
}

void Power::refresh() {
    if (!manager_) {
        return;
    }
    for (std::size_t i = 0; i < kActions; ++i) {
        try {
            manager_->callMethodAsync(kNames.at(i).can)
                .onInterface(kManager)
                .uponReplyInvoke([this, i](std::optional<sdbus::Error> error, std::string answer) {
                    // "challenge": allowed after authenticating.
                    const bool can = !error && (answer == "yes" || answer == "challenge");
                    if (can != can_.at(i)) {
                        can_.at(i) = can;
                        if (on_changed) {
                            on_changed();
                        }
                    }
                });
        } catch (const sdbus::Error& e) {
            std::println(stderr, "power actions: {}", e.what());
            return;
        }
    }
}

bool Power::can(Action action) const noexcept {
    return can_[static_cast<std::size_t>(action)];
}

void Power::request(Action action) {
    if (!manager_ || !can(action)) {
        return;
    }
    const auto& names = kNames.at(static_cast<std::size_t>(action));
    try {
        manager_->callMethodAsync(names.request)
            .onInterface(kManager)
            .withArguments(true) // interactive: polkit may ask
            .uponReplyInvoke([request = names.request](std::optional<sdbus::Error> error) {
                if (error) {
                    std::println(stderr, "{} refused: {}", request, error->what());
                }
            });
    } catch (const sdbus::Error& e) {
        std::println(stderr, "{}: {}", names.request, e.what());
    }
}

const char* Power::label(Action action) {
    return kNames.at(static_cast<std::size_t>(action)).label;
}

} // namespace tetra::ui
