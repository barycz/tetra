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

DBusConnection::DBusConnection(App& app, std::unique_ptr<sdbus::IConnection> connection)
    : app_(app), connection_(std::move(connection)) {
    source_ = app_.add_source(
        [this](std::vector<pollfd>& fds) {
            const auto poll = connection_->getEventLoopPollData();
            fds.push_back({.fd = poll.fd, .events = poll.events, .revents = 0});
            fds.push_back({.fd = poll.eventFd, .events = POLLIN, .revents = 0});
            return poll.getPollTimeout();
        },
        [this] {
            while (connection_->processPendingEvent()) {
            }
        });
}

DBusConnection::~DBusConnection() {
    app_.remove_source(source_);
}

} // namespace tetra::ui
