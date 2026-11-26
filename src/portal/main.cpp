// tetra-portal: tetra's xdg-desktop-portal backend. Started by D-Bus
// activation (org.freedesktop.impl.portal.desktop.tetra) when
// xdg-desktop-portal needs it, inside a tetra session.

#include "app.hpp"
#include "dbus.hpp"
#include "portal.hpp"

#include <clocale>
#include <print>

int main() {
    std::setlocale(LC_ALL, "");
    auto created = tetra::ui::App::create("tetra-portal");
    if (!created) {
        std::println(stderr, "tetra-portal: {}", created.error());
        return 1;
    }
    tetra::ui::App& app = **created;
    auto bus = tetra::ui::DBusConnection::session(app);
    if (!bus) {
        return 1;
    }
    int code = 0;
    {
        tetra::portal::Portal portal(app, bus->connection());
        try {
            // Last: the objects are there before anyone can call.
            bus->connection().requestName(sdbus::ServiceName{tetra::portal::Portal::kBusName});
        } catch (const sdbus::Error& e) {
            std::println(stderr, "tetra-portal: {} (already running?)", e.getMessage());
            return 1;
        }
        code = app.run();
    }
    return code;
}
