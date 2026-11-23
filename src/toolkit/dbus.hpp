#pragma once

#include "app.hpp"

#include <sdbus-c++/sdbus-c++.h>

#include <memory>

namespace tetra::ui {

// A D-Bus connection driven by App's event loop (no extra thread): method
// handlers and signal callbacks run on the UI thread, between frames.
class DBusConnection {
public:
    // The session bus; null (with a message on stderr) if there is none.
    static std::unique_ptr<DBusConnection> session(App& app);
    // The system bus (logind, UPower, NetworkManager, polkit); null if none.
    static std::unique_ptr<DBusConnection> system(App& app);
    ~DBusConnection();

    DBusConnection(const DBusConnection&) = delete;
    DBusConnection& operator=(const DBusConnection&) = delete;
    DBusConnection(DBusConnection&&) = delete;
    DBusConnection& operator=(DBusConnection&&) = delete;

    [[nodiscard]] sdbus::IConnection& connection() noexcept { return *connection_; }

private:
    DBusConnection(App& app, std::unique_ptr<sdbus::IConnection> connection);

    App& app_;
    std::unique_ptr<sdbus::IConnection> connection_;
    App::SourceId source_ = 0;
};

} // namespace tetra::ui
