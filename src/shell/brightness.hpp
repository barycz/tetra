#pragma once

#include <sdbus-c++/sdbus-c++.h>

#include <filesystem>
#include <memory>
#include <string>

namespace tetra::shell {

// Backlight brightness of the built-in display: read from sysfs, set through
// logind (org.freedesktop.login1.Session.SetBrightness), which needs no
// root. Unavailable without a backlight device (desktops, VMs).
class Brightness {
public:
    // `system_bus` may be null (no system bus): then unavailable.
    explicit Brightness(sdbus::IConnection* system_bus);

    [[nodiscard]] bool available() const noexcept { return session_ != nullptr; }
    // 0..1; 0 if unavailable.
    [[nodiscard]] float level() const;
    // Returns the new level.
    float change(float delta);

private:
    std::filesystem::path device_;
    std::string name_;
    int max_ = 0;
    std::unique_ptr<sdbus::IProxy> session_;
};

} // namespace tetra::shell
