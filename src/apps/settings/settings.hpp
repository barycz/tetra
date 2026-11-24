#pragma once

#include "app.hpp"
#include "config_edit.hpp"
#include "desktop_entry.hpp"
#include "displays.hpp"
#include "icons.hpp"
#include "window.hpp"
#include "xkb.hpp"

#include <imgui.h>

#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tetra::settings {

// A display's area in layout pixels (Displays page).
struct Rect {
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
};

// tetra-settings: an editor for config.json. Every change is written at
// once (keeping the user's comments) and the whole desktop picks it up
// through live reload; the values shown always come from the file.
class Settings {
public:
    explicit Settings(ui::App& app);

    // For tests and the command line: open on a given page ("appearance", ...).
    void show_page(std::string_view id);

private:
    using Edit = std::function<std::expected<void, std::string>(config::Document&)>;

    enum class Page { Appearance, Displays, Zones, Taskbar, Keyboard, Mouse, Power, Apps, About };
    struct PageInfo {
        Page page;
        const char* id;
        const char* title;
        const char* icon;
    };
    static const std::vector<PageInfo>& pages();

    void draw();
    void draw_sidebar(float width, float height);
    void draw_status();

    // Pages (pages.cpp).
    void page_appearance();
    void page_displays();
    void page_zones();
    // Snap zone layouts as thumbnails; the one clicked, if any (zones_page.cpp).
    std::optional<std::string> layout_picker(const char* id, const std::string& current);
    void page_taskbar();
    void page_keyboard();
    void page_mouse();
    // A -1..1 pointer speed slider for config key `section`.speed.
    void speed_slider(const char* id, const char* section, double value, std::optional<double>& editing);
    void page_power();
    void page_apps();
    void page_about();

    // Writes one change; shows the error if the file cannot take it.
    bool save(const Edit& edit);

    // Displays (display_page.cpp). Changes are JSON members per display
    // name, e.g. {"scale", "1.5"}; they can be reverted for a few seconds.
    using DisplayChanges = std::vector<std::pair<std::string, std::string>>;
    [[nodiscard]] std::string display_members(const std::string& name, const DisplayChanges& changes) const;
    void change_displays(const std::vector<std::pair<std::string, DisplayChanges>>& changes);
    void place_display(const std::string& name, const Rect& dropped);
    void tick_revert();
    void keep_display_changes();
    void revert_display_changes();
    void draw_revert_banner();
    void display_arrangement();
    // Layout helpers: a titled card of rows, each with a label (and an
    // optional hint) on the left and its control on the right.
    void begin_card(const char* title);
    void end_card();
    void row(const char* label, const char* hint = nullptr);
    [[nodiscard]] float control_width() const;
    bool toggle(const char* id, bool value);
    bool color_swatch(const char* id, const config::Color& color, bool selected, float size);
    bool icon_button(const char* id, const char* icon_name, bool enabled);

    ui::App& app_;
    ui::IconCache icons_;
    DesktopDatabase applications_;
    Xkb xkb_;
    Displays displays_;
    std::vector<std::string> font_families_;
    std::unique_ptr<ui::ToplevelWindow> window_;
    Page page_ = Page::Appearance;
    std::string error_;            // the last save that failed
    std::string load_error_;       // config.json does not load as it is
    // Edits in progress (written when the user lets go).
    std::optional<double> font_size_;
    std::optional<int> titlebar_height_;
    std::optional<int> border_width_;
    std::optional<int> repeat_delay_;
    std::optional<int> repeat_rate_;
    std::optional<double> mouse_speed_;
    std::optional<double> touchpad_speed_;
    std::optional<int> zone_gap_;
    // Text being typed (fields show the config value while not focused).
    std::array<char, 256> filter_buffer_{}; // search in combos
    std::array<char, 256> accent_buffer_{};
    std::array<char, 256> background_buffer_{};
    std::array<char, 256> terminal_buffer_{};
    std::array<char, 256> autostart_buffer_{};
    std::array<char, 256> shell_buffer_{};
    std::array<char, 256> test_buffer_{};
    // Displays: a change waiting to be kept, and a display being dragged.
    struct Revert {
        std::optional<std::string> before; // the file before the first change (none: no file)
        std::string after;                 // what the last change wrote
        std::chrono::steady_clock::time_point deadline;
    };
    std::optional<Revert> revert_;
    std::optional<ui::App::TimerId> revert_timer_;
    struct Mapping { // layout -> screen
        double scale = 1;
        double x = 0;
        double y = 0;
    };
    struct Drag {
        std::string name;
        ImVec2 start_mouse;
        Rect start;
        Mapping mapping;
    };
    std::optional<Drag> drag_;
    int card_rows_ = 0;
    float row_bottom_ = 0; // where the current row's label ends
};

} // namespace tetra::settings
