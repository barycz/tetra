#include "logind.hpp"

#include <cstdint>
#include <print>
#include <string>

#include <unistd.h>

namespace tetra::shell {

namespace {

const sdbus::ServiceName kService{"org.freedesktop.login1"};
constexpr const char* kManager = "org.freedesktop.login1.Manager";
constexpr const char* kSession = "org.freedesktop.login1.Session";

} // namespace

Logind::Logind(sdbus::IConnection& system_bus) : bus_(system_bus) {
    try {
        manager_ = sdbus::createProxy(bus_, kService, sdbus::ObjectPath{"/org/freedesktop/login1"});
        sdbus::ObjectPath path;
        manager_->callMethod("GetSessionByPID")
            .onInterface(kManager)
            .withArguments(static_cast<uint32_t>(getpid()))
            .storeResultsTo(path);
        session_ = sdbus::createProxy(bus_, kService, std::move(path));
        session_id_ = session_->getProperty("Id").onInterface(kSession).get<std::string>();
        session_->uponSignal("Lock").onInterface(kSession).call([this] {
            if (on_lock) {
                on_lock();
            }
        });
        manager_->uponSignal("PrepareForSleep").onInterface(kManager).call([this](bool start) {
            if (start) {
                if (lock_before_sleep_ && on_lock) {
                    on_lock(); // the inhibitor is released once the lock is up
                } else {
                    release_sleep_inhibitor();
                }
            } else {
                take_sleep_inhibitor(); // ready for the next time
            }
        });
    } catch (const sdbus::Error& e) {
        // Headless tests, containers, sessions not started through logind.
        std::println(stderr, "tetra-shell: logind integration disabled: {}", e.what());
        session_.reset();
        manager_.reset();
        session_id_.clear();
    }
}

void Logind::set_lock_before_sleep(bool enabled) {
    lock_before_sleep_ = enabled;
    if (enabled) {
        take_sleep_inhibitor();
    } else {
        release_sleep_inhibitor();
    }
}

void Logind::take_sleep_inhibitor() {
    if (!manager_ || !lock_before_sleep_ || sleep_inhibitor_) {
        return;
    }
    try {
        sdbus::UnixFd fd;
        manager_->callMethod("Inhibit")
            .onInterface(kManager)
            .withArguments(std::string("sleep"), std::string("tetra"),
                           std::string("Lock the screen before sleeping"), std::string("delay"))
            .storeResultsTo(fd);
        sleep_inhibitor_ = std::move(fd);
    } catch (const sdbus::Error& e) {
        std::println(stderr, "tetra-shell: cannot delay sleep to lock first: {}", e.what());
    }
}

void Logind::release_sleep_inhibitor() {
    sleep_inhibitor_.reset(); // closing the fd releases it
}

} // namespace tetra::shell
