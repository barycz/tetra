#include "dbus.hpp"

#include <print>

namespace tetra::ui {

std::unique_ptr<DBusConnection> DBusConnection::session(App& app) {
    try {
        return std::unique_ptr<DBusConnection>(
            new DBusConnection(app, sdbus::createSessionBusConnection()));
    } catch (const sdbus::Error& e) {
        std::println(stderr, "{}: no D-Bus session bus: {}", app.app_id(), e.getMessage());
        return nullptr;
    }
}

std::unique_ptr<DBusConnection> DBusConnection::system(App& app) {
    try {
        return std::unique_ptr<DBusConnection>(
            new DBusConnection(app, sdbus::createSystemBusConnection()));
    } catch (const sdbus::Error& e) {
        std::println(stderr, "{}: no D-Bus system bus: {}", app.app_id(), e.getMessage());
        return nullptr;
    }
}

DBusConnection::DBusConnection(App& app, std::unique_ptr<sdbus::IConnection> connection)
    : app_(app), connection_(std::move(connection)) {
    source_ = app_.add_source(
        [this](std::vector<pollfd>& fds) {
            if (broken_) {
                return -1;
            }
            try {
                const auto poll = connection_->getEventLoopPollData();
                fds.push_back({.fd = poll.fd, .events = poll.events, .revents = 0});
                fds.push_back({.fd = poll.eventFd, .events = POLLIN, .revents = 0});
                return poll.getPollTimeout();
            } catch (const sdbus::Error& e) {
                // The bus daemon went away (e.g. at logout); nothing more to do.
                std::println(stderr, "{}: D-Bus connection lost: {}", app_.app_id(), e.what());
                broken_ = true;
                return -1;
            }
        },
        [this] {
            if (broken_) {
                return;
            }
            try {
                while (connection_->processPendingEvent()) {
                }
            } catch (const sdbus::Error& e) {
                std::println(stderr, "{}: D-Bus connection lost: {}", app_.app_id(), e.what());
                broken_ = true;
            }
        });
}

DBusConnection::~DBusConnection() {
    app_.remove_source(source_);
}

} // namespace tetra::ui
