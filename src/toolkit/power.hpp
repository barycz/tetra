#pragma once

#include <sdbus-c++/sdbus-c++.h>

#include <array>
#include <functional>
#include <memory>
#include <string>

namespace tetra::ui {

// Suspend, restart and power off through logind (system bus), for the
// greeter and the launcher's power menu. Which actions exist comes from
// logind's Can* answers (asynchronous); actions are requested with
// interactive=true, so polkit may ask for a password when needed.
class Power {
public:
    enum class Action { Suspend = 0, Reboot = 1, PowerOff = 2 };
    static constexpr std::size_t kActions = 3;

    // `system_bus` may be null (no system bus): nothing is possible.
    explicit Power(sdbus::IConnection* system_bus);

    // Asks logind again; on_changed follows each answer.
    void refresh();
    [[nodiscard]] bool can(Action action) const noexcept;
    void request(Action action);

    static const char* label(Action action);

    std::function<void()> on_changed;

private:
    std::unique_ptr<sdbus::IProxy> manager_;
    std::array<bool, kActions> can_{};
};

} // namespace tetra::ui
