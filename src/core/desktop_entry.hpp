#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Installed applications, from freedesktop .desktop files.
namespace tetra {

struct DesktopEntry {
    std::string id; // desktop file ID, e.g. "org.gnome.Nautilus" (no ".desktop")
    std::string name;
    std::string generic_name;
    std::string comment;
    std::string icon;
    std::string exec;
    std::string wm_class; // StartupWMClass
    std::vector<std::string> keywords;
    std::vector<std::string> categories;
    bool terminal = false;
    bool hidden_from_menus = false; // NoDisplay, or not meant for this desktop
    std::filesystem::path path;
};

class DesktopDatabase {
public:
    // Scans $XDG_DATA_HOME/applications and $XDG_DATA_DIRS/*/applications.
    void load();

    // Applications to show in menus, sorted by name.
    [[nodiscard]] std::vector<const DesktopEntry*> visible() const;
    [[nodiscard]] const DesktopEntry* find(std::string_view id) const;
    // Best entry for a Wayland app_id or X11 WM_CLASS; null if none.
    [[nodiscard]] const DesktopEntry* match_app_id(std::string_view app_id) const;

private:
    std::vector<DesktopEntry> entries_;
};

// argv for launching `entry`: field codes removed, quoting resolved, and
// wrapped in `terminal` (e.g. "foot") if the entry runs in a terminal.
std::vector<std::string> exec_argv(const DesktopEntry& entry, const std::string& terminal);

// Case-insensitive ASCII comparison, used for app_id matching.
bool iequals(std::string_view a, std::string_view b);

} // namespace tetra
