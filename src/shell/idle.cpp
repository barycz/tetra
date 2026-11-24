#include "idle.hpp"

#include "protocols.hpp"

#include <algorithm>
#include <utility>

namespace tetra::shell {

namespace {

// On the lock screen nobody is working: blank the screens after this long.
constexpr int kLockedScreenOffSeconds = 60;

const zwlr_output_power_v1_listener kPowerListener = {
    .mode = [](void* /*data*/, zwlr_output_power_v1* /*power*/, uint32_t /*mode*/) {},
    .failed = [](void* /*data*/, zwlr_output_power_v1* /*power*/) {},
};

} // namespace

struct IdleListener {
    static const ext_idle_notification_v1_listener listener;
};

const ext_idle_notification_v1_listener IdleListener::listener = {
    .idled =
        [](void* data, ext_idle_notification_v1* /*notification*/) {
            auto* timer = static_cast<Idle::Timer*>(data);
            if (timer->on_idle) {
                timer->on_idle();
            }
        },
    .resumed =
        [](void* data, ext_idle_notification_v1* /*notification*/) {
            auto* timer = static_cast<Idle::Timer*>(data);
            if (timer->on_resume) {
                timer->on_resume();
            }
        },
};

Idle::Timer::~Timer() {
    if (notification != nullptr) {
        ext_idle_notification_v1_destroy(notification);
    }
}

Idle::Idle(wl_seat* seat, ext_idle_notifier_v1* notifier, zwlr_output_power_manager_v1* power)
    : seat_(seat), notifier_(notifier), power_(power) {}

Idle::~Idle() {
    lock_timer_.reset();
    screen_timer_.reset();
    for (auto& [global, power] : outputs_) {
        zwlr_output_power_v1_destroy(power);
    }
    if (power_ != nullptr) {
        zwlr_output_power_manager_v1_destroy(power_);
    }
    if (notifier_ != nullptr) {
        ext_idle_notifier_v1_destroy(notifier_);
    }
}

std::unique_ptr<Idle::Timer> Idle::make_timer(int seconds, std::function<void()> on_idle,
                                              std::function<void()> on_resume) {
    if (seconds <= 0 || notifier_ == nullptr || seat_ == nullptr) {
        return nullptr;
    }
    auto timer = std::make_unique<Timer>();
    timer->idle = this;
    timer->on_idle = std::move(on_idle);
    timer->on_resume = std::move(on_resume);
    timer->notification = ext_idle_notifier_v1_get_idle_notification(
        notifier_, static_cast<uint32_t>(seconds) * 1000, seat_);
    ext_idle_notification_v1_add_listener(timer->notification, &IdleListener::listener,
                                          timer.get());
    return timer;
}

void Idle::configure(const config::Idle& idle, bool locked) {
    if (configured_ && idle == config_ && locked == locked_) {
        return;
    }
    configured_ = true;
    config_ = idle;
    locked_ = locked;

    lock_timer_.reset();
    screen_timer_.reset();
    if (screens_off_) {
        set_screens(true); // the new timer never saw them go off, so it cannot wake them
    }
    if (!locked) {
        lock_timer_ = make_timer(idle.lock_after, [this] {
            if (on_lock) {
                on_lock();
            }
        }, nullptr);
    }
    int screen_off = idle.screen_off_after;
    if (locked) {
        screen_off = screen_off > 0 ? std::min(screen_off, kLockedScreenOffSeconds)
                                    : kLockedScreenOffSeconds;
    }
    screen_timer_ = make_timer(screen_off, [this] { set_screens(false); },
                               [this] { set_screens(true); });
}

void Idle::add_output(uint32_t global, wl_output* output) {
    if (power_ == nullptr || outputs_.contains(global)) {
        return;
    }
    zwlr_output_power_v1* power = zwlr_output_power_manager_v1_get_output_power(power_, output);
    zwlr_output_power_v1_add_listener(power, &kPowerListener, this);
    outputs_[global] = power;
    if (screens_off_) {
        zwlr_output_power_v1_set_mode(power, ZWLR_OUTPUT_POWER_V1_MODE_OFF);
    }
}

void Idle::remove_output(uint32_t global) {
    if (auto it = outputs_.find(global); it != outputs_.end()) {
        zwlr_output_power_v1_destroy(it->second);
        outputs_.erase(it);
    }
}

void Idle::set_screens(bool on) {
    screens_off_ = !on;
    for (auto& [global, power] : outputs_) {
        zwlr_output_power_v1_set_mode(power, on ? ZWLR_OUTPUT_POWER_V1_MODE_ON
                                                : ZWLR_OUTPUT_POWER_V1_MODE_OFF);
    }
}

} // namespace tetra::shell
