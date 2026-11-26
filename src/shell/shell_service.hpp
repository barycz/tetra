#pragma once

#include <sdbus-c++/sdbus-c++.h>

#include <memory>

namespace tetra::shell {

class Shell;

// org.tetra.Shell1 on the session bus (name org.tetra.Shell): what tetra's
// own portal backend (tetra-portal) asks of the shell.
//
//   Screenshot(b interactive) -> (s path)
//     The picture Print (interactive: Shift+Print, the region picker)
//     would take, saved under Pictures/Screenshots. Errors:
//     org.tetra.Shell1.Error.Cancelled, org.tetra.Shell1.Error.Failed.
//
// Only the owner of org.freedesktop.impl.portal.desktop.tetra may call it:
// apps go through xdg-desktop-portal, which asks the user when needed.
class ShellService {
public:
    ShellService(Shell& shell, sdbus::IConnection& connection);
    ~ShellService();

    ShellService(const ShellService&) = delete;
    ShellService& operator=(const ShellService&) = delete;
    ShellService(ShellService&&) = delete;
    ShellService& operator=(ShellService&&) = delete;

private:
    [[nodiscard]] bool caller_is_portal();

    Shell& shell_;
    sdbus::IConnection& connection_;
    std::unique_ptr<sdbus::IObject> object_;
    std::unique_ptr<sdbus::IProxy> bus_;
};

} // namespace tetra::shell
