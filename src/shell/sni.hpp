#pragma once

#include <sdbus-c++/sdbus-c++.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tetra::ui {
class App;
class Texture;
}

namespace tetra::shell {

// One app's tray icon (org.kde.StatusNotifierItem), as last read.
struct SniItem {
    struct Pixmap {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> argb; // ARGB32, network byte order
    };

    std::string id;      // as registered: bus name + object path
    std::string service; // bus name
    std::string path;    // object path
    std::unique_ptr<sdbus::IProxy> proxy;

    std::string title;
    std::string status = "Active"; // Passive (hidden), Active, NeedsAttention
    std::string icon_name;
    std::string attention_icon_name;
    std::string icon_theme_path;
    std::string tooltip;
    std::string menu_path; // com.canonical.dbusmenu object, if any
    bool item_is_menu = false;
    std::vector<Pixmap> icon_pixmaps;
    std::vector<Pixmap> attention_pixmaps;
    bool ready = false; // properties read at least once

    // Drawn from the pixmaps when there is no icon name; rebuilt on change.
    std::unique_ptr<ui::Texture> texture;
    bool texture_dirty = true;

    SniItem();
    ~SniItem();
    SniItem(const SniItem&) = delete;
    SniItem& operator=(const SniItem&) = delete;
    SniItem(SniItem&&) = delete;
    SniItem& operator=(SniItem&&) = delete;

    [[nodiscard]] bool visible() const { return ready && status != "Passive"; }
    [[nodiscard]] bool needs_attention() const { return status == "NeedsAttention"; }
    // The pixmap closest to `size` pixels (bigger preferred), or null.
    [[nodiscard]] const Pixmap* best_pixmap(int size) const;
};

// The system tray for other apps (StatusNotifierItem): we are the watcher
// (org.kde.StatusNotifierWatcher) and the only host. Apps register their
// items with us; we read their properties asynchronously (a hung app never
// blocks the shell) and follow their change signals; items whose bus name
// goes away are dropped.
class SniTray {
public:
    // Throws sdbus::Error if another watcher owns the name.
    SniTray(ui::App& app, sdbus::IConnection& session_bus);
    ~SniTray();

    SniTray(const SniTray&) = delete;
    SniTray& operator=(const SniTray&) = delete;
    SniTray(SniTray&&) = delete;
    SniTray& operator=(SniTray&&) = delete;

    [[nodiscard]] const std::vector<std::unique_ptr<SniItem>>& items() const noexcept {
        return items_;
    }
    // Items were added, removed or changed.
    std::function<void()> on_changed;

    // Clicks, in output coordinates. `on_menu` runs when the item wants its
    // menu shown instead (ItemIsMenu, or no Activate).
    void activate(SniItem& item, int x, int y, std::function<void()> on_menu);
    void secondary_activate(SniItem& item, int x, int y);
    void context_menu(SniItem& item, int x, int y);
    void scroll(SniItem& item, int delta);

private:
    void register_item(const std::string& sender, const std::string& service);
    void owner_gone(const std::string& name);
    void refresh(SniItem& item);
    void changed();
    [[nodiscard]] std::vector<std::string> registered() const;

    ui::App& app_;
    sdbus::IConnection& bus_;
    std::unique_ptr<sdbus::IObject> watcher_;
    std::unique_ptr<sdbus::IProxy> dbus_; // org.freedesktop.DBus: NameOwnerChanged
    std::vector<std::unique_ptr<SniItem>> items_;
};

} // namespace tetra::shell
