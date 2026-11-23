#pragma once

#include "app.hpp"
#include "texture.hpp"
#include "window.hpp"

#include <sdbus-c++/sdbus-c++.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tetra::shell {

class Shell;

struct Notification {
    struct Image {
        int width = 0;
        int height = 0;
        int stride = 0;
        int channels = 0;
        std::vector<uint8_t> pixels;
    };

    uint32_t id = 0;
    std::string app_name;
    std::string icon; // icon name or absolute path; resolved from hints and the desktop entry
    std::string summary;
    std::string body;
    std::vector<std::pair<std::string, std::string>> actions; // key, label
    uint8_t urgency = 1;                                     // 0 low, 1 normal, 2 critical
    bool resident = false;
    int32_t timeout_ms = 0; // 0: stays until dismissed
    std::optional<Image> image;
    std::unique_ptr<ui::Texture> texture; // created from `image` when first drawn
    std::optional<ui::App::TimerId> timer;
    bool hovered = false;
};

class NotificationPopups;

// org.freedesktop.Notifications on the session bus (spec 1.2): receives
// notifications from apps and shows them as popups above the bar.
class Notifications {
public:
    enum class CloseReason : uint32_t { Expired = 1, Dismissed = 2, Closed = 3 };

    // Throws sdbus::Error if the bus name cannot be acquired.
    Notifications(Shell& shell, sdbus::IConnection& connection);
    ~Notifications();

    Notifications(const Notifications&) = delete;
    Notifications& operator=(const Notifications&) = delete;
    Notifications(Notifications&&) = delete;
    Notifications& operator=(Notifications&&) = delete;

    [[nodiscard]] const std::vector<std::unique_ptr<Notification>>& list() const noexcept {
        return list_;
    }
    [[nodiscard]] Shell& shell() noexcept { return shell_; }

    void dismiss(uint32_t id);
    void invoke(uint32_t id, const std::string& action);
    void set_hovered(uint32_t id, bool hovered);

private:
    uint32_t notify(const std::string& app_name, uint32_t replaces_id, const std::string& app_icon,
                    const std::string& summary, const std::string& body,
                    const std::vector<std::string>& actions,
                    const std::map<std::string, sdbus::Variant>& hints, int32_t expire_timeout);
    void close(uint32_t id, CloseReason reason);
    void arm_timer(Notification& notification);
    Notification* find(uint32_t id);

    Shell& shell_;
    sdbus::IConnection& connection_;
    std::unique_ptr<sdbus::IObject> object_;
    std::vector<std::unique_ptr<Notification>> list_; // oldest first
    uint32_t next_id_ = 1;
    std::unique_ptr<NotificationPopups> popups_;
};

// The popup stack: one layer surface at the bottom-right of the output
// under the pointer, sized to its content.
class NotificationPopups {
public:
    explicit NotificationPopups(Notifications& notifications);

    // The list changed: create, redraw or (deferred) destroy the window.
    void update();

private:
    void draw();
    float draw_card(Notification& notification, float y, float width);

    Notifications& notifications_;
    std::shared_ptr<ui::LayerWindow> window_;
};

} // namespace tetra::shell
