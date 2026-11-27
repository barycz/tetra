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
    std::vector<std::string> desktop_names; // DesktopNames (session files)
    bool terminal = false;
    bool no_display = false;
    bool not_for_tetra = false;     // OnlyShowIn/NotShowIn leave tetra out
    bool hidden_from_menus = false; // no_display or not_for_tetra
    bool autostart_enabled = true;  // X-GNOME-Autostart-enabled
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

// Every entry in `dirs` (highest priority first). An ID found in an earlier
// directory hides the same ID in later ones, also when it is Hidden=true
// (that is how a user turns off a system-wide autostart entry).
std::vector<DesktopEntry> read_entries(const std::vector<std::filesystem::path>& dirs);

// XDG autostart: $XDG_CONFIG_HOME/autostart, then each of $XDG_CONFIG_DIRS
// (default /etc/xdg) + /autostart.
std::vector<std::filesystem::path> autostart_dirs();
// The entries to start when a session begins: meant for tetra and enabled.
std::vector<DesktopEntry> autostart_entries(const std::vector<std::filesystem::path>& dirs);

// Wayland sessions a greeter offers: each of $XDG_DATA_DIRS +
// /wayland-sessions.
std::vector<std::filesystem::path> session_dirs();
// Sessions to offer (NoDisplay left out), sorted by name.
std::vector<DesktopEntry> session_entries(const std::vector<std::filesystem::path>& dirs);

// argv for launching `entry`: field codes removed, quoting resolved, and
// wrapped in `terminal` (e.g. "tetra-terminal") if the entry runs in a terminal.
std::vector<std::string> exec_argv(const DesktopEntry& entry, const std::string& terminal);

// Case-insensitive ASCII comparison, used for app_id matching.
bool iequals(std::string_view a, std::string_view b);

} // namespace tetra
