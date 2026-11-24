#pragma once

#include "app.hpp"
#include "config_edit.hpp"
#include "desktop_entry.hpp"
#include "icons.hpp"
#include "window.hpp"
#include "xkb.hpp"

#include <imgui.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tetra::settings {

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

    enum class Page { Appearance, Taskbar, Keyboard, Power, Apps, About };
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
    void page_taskbar();
    void page_keyboard();
    void page_power();
    void page_apps();
    void page_about();

    // Writes one change; shows the error if the file cannot take it.
    void save(const Edit& edit);
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
    // Text being typed (fields show the config value while not focused).
    std::array<char, 256> filter_buffer_{}; // search in combos
    std::array<char, 256> accent_buffer_{};
    std::array<char, 256> background_buffer_{};
    std::array<char, 256> terminal_buffer_{};
    std::array<char, 256> autostart_buffer_{};
    std::array<char, 256> shell_buffer_{};
    std::array<char, 256> test_buffer_{};
    int card_rows_ = 0;
    float row_bottom_ = 0; // where the current row's label ends
};

} // namespace tetra::settings
