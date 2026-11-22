#pragma once

#include <optional>
#include <string>

namespace tetra::shell {

// Status indicators next to the clock. Read from sysfs for now; D-Bus
// services (NetworkManager, UPower) and volume come with phase 3.
class Tray {
public:
    struct State {
        std::string network_icon; // symbolic icon name
        std::string network_label;
        std::optional<int> battery_percent; // no battery: nullopt
        std::string battery_icon;
        bool operator==(const State&) const = default;
    };

    // Re-reads the system state; true if anything changed.
    bool refresh();
    [[nodiscard]] const State& state() const noexcept { return state_; }

private:
    State state_;
};

} // namespace tetra::shell
