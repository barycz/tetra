#pragma once

#include "config.hpp"

#include <wayland-client.h>

#include <functional>
#include <map>
#include <memory>

struct ext_idle_notification_v1;
struct ext_idle_notifier_v1;
struct zwlr_output_power_manager_v1;
struct zwlr_output_power_v1;

namespace tetra::shell {

// Idle handling (ext-idle-notify-v1): locks after `idle.lock_after` seconds
// without input and turns the screens off after `idle.screen_off_after`
// (wlr-output-power-management); any input turns them back on. On the lock
// screen the screens go off sooner. Apps can inhibit idling (video).
class Idle {
public:
    Idle(wl_seat* seat, ext_idle_notifier_v1* notifier, zwlr_output_power_manager_v1* power);
    ~Idle();

    Idle(const Idle&) = delete;
    Idle& operator=(const Idle&) = delete;
    Idle(Idle&&) = delete;
    Idle& operator=(Idle&&) = delete;

    std::function<void()> on_lock;

    // (Re)creates the timers; call when the configuration or the lock state changes.
    void configure(const config::Idle& idle, bool locked);
    void add_output(uint32_t global, wl_output* output);
    void remove_output(uint32_t global);

private:
    friend struct IdleListener;
    struct Timer {
        Timer() = default;
        ~Timer();
        Timer(const Timer&) = delete;
        Timer& operator=(const Timer&) = delete;
        Timer(Timer&&) = delete;
        Timer& operator=(Timer&&) = delete;

        Idle* idle = nullptr;
        ext_idle_notification_v1* notification = nullptr;
        std::function<void()> on_idle;
        std::function<void()> on_resume;
    };
    std::unique_ptr<Timer> make_timer(int seconds, std::function<void()> on_idle,
                                      std::function<void()> on_resume);
    void set_screens(bool on);

    wl_seat* seat_;
    ext_idle_notifier_v1* notifier_;
    zwlr_output_power_manager_v1* power_;
    std::unique_ptr<Timer> lock_timer_;
    std::unique_ptr<Timer> screen_timer_;
    std::map<uint32_t, zwlr_output_power_v1*> outputs_;
    bool screens_off_ = false;
    config::Idle config_;
    bool locked_ = false;
    bool configured_ = false;
};

} // namespace tetra::shell
