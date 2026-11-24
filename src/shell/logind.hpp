#pragma once

#include <sdbus-c++/sdbus-c++.h>

#include <functional>
#include <memory>
#include <optional>

namespace tetra::shell {

// The session's logind side, on the system bus:
// - Session.Lock (`loginctl lock-session`, lid switch handlers) -> on_lock
// - before suspend/hibernate: a delay inhibitor gives us time to lock;
//   release_sleep_inhibitor() once the lock screen is up lets sleep proceed.
// Without logind (or outside a logind session) it does nothing.
class Logind {
public:
    explicit Logind(sdbus::IConnection& system_bus);

    // Called when logind asks to lock, or just before the system sleeps.
    std::function<void()> on_lock;

    // Whether to lock before sleeping (idle.lock_before_sleep).
    void set_lock_before_sleep(bool enabled);
    // The lock screen is up: sleep may go ahead.
    void release_sleep_inhibitor();

private:
    void take_sleep_inhibitor();

    sdbus::IConnection& bus_;
    std::unique_ptr<sdbus::IProxy> manager_;
    std::unique_ptr<sdbus::IProxy> session_;
    std::optional<sdbus::UnixFd> sleep_inhibitor_;
    bool lock_before_sleep_ = false;
};

} // namespace tetra::shell
