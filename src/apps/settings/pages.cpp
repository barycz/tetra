// The pages of tetra-settings. Each control shows the value from the loaded
// configuration and writes changes straight into config.json.

#include "settings.hpp"

#include "config.hpp"
#include "mark.hpp"
#include "process.hpp"
#include "style.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <ranges>
#include <sstream>
#include <unistd.h>

#include <sys/utsname.h>

#ifndef TETRA_VERSION
#define TETRA_VERSION "dev"
#endif

namespace tetra::settings {

namespace {

using config::Color;
namespace fs = std::filesystem;

struct Swatch {
    const char* name;
    const char* hex;
};

constexpr std::array kAccents = {
    Swatch{"Blue", "#4c8dff"},   Swatch{"Teal", "#2ec4b6"},   Swatch{"Green", "#3fb950"},
    Swatch{"Yellow", "#e3b341"}, Swatch{"Orange", "#f0883e"}, Swatch{"Red", "#f85149"},
    Swatch{"Pink", "#db61a2"},   Swatch{"Purple", "#a371f7"},
};

constexpr std::array kBackgrounds = {
    Swatch{"Graphite", "#1b1d22"}, Swatch{"Slate", "#262a33"},  Swatch{"Navy", "#18243a"},
    Swatch{"Forest", "#1b2a22"},   Swatch{"Plum", "#2a1f33"},   Swatch{"Stone", "#55595f"},
    Swatch{"Mist", "#c9ced6"},     Swatch{"Paper", "#eef0f3"},
};

struct Duration {
    int seconds;
    const char* label;
};

constexpr std::array kLockTimes = {
    Duration{0, "Never"},         Duration{60, "1 minute"},    Duration{120, "2 minutes"},
    Duration{300, "5 minutes"},   Duration{600, "10 minutes"}, Duration{900, "15 minutes"},
    Duration{1800, "30 minutes"}, Duration{3600, "1 hour"},
};

constexpr std::array kTerminals = {"tetra-terminal", "foot",           "kitty",
                                   "alacritty",      "wezterm",        "gnome-terminal",
                                   "kgx",            "konsole",        "xfce4-terminal",
                                   "xterm"};

std::string duration_label(int seconds) {
    for (const auto& d : kLockTimes) {
        if (d.seconds == seconds) {
            return d.label;
        }
    }
    return seconds % 60 == 0 ? std::format("{} minutes", seconds / 60) : std::format("{} seconds", seconds);
}

bool contains_ci(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    return std::ranges::search(haystack, needle, {}, lower, lower).begin() != haystack.end();
}

bool on_path(const std::string& program) {
    const char* path = std::getenv("PATH"); // NOLINT(concurrency-mt-unsafe)
    if (path == nullptr) {
        return false;
    }
    std::stringstream dirs(path);
    std::string dir;
    while (std::getline(dirs, dir, ':')) {
        if (!dir.empty() && access((dir + "/" + program).c_str(), X_OK) == 0) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> split_options(const std::string& options) {
    std::vector<std::string> out;
    std::stringstream in(options);
    std::string item;
    while (std::getline(in, item, ',')) {
        item.erase(0, item.find_first_not_of(" \t"));
        item.erase(item.find_last_not_of(" \t") + 1);
        if (!item.empty()) {
            out.push_back(item);
        }
    }
    return out;
}

std::string join(const std::vector<std::string>& items, const char* separator) {
    std::string out;
    for (const auto& item : items) {
        out += out.empty() ? item : separator + item;
    }
    return out;
}

// A two-or-more-way choice drawn as joined buttons.
bool segment(const theme::Palette& p, const char* label, bool selected, float width) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetFrameHeight() + 6;
    const bool clicked = ImGui::InvisibleButton(label, {width, height});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 bg = selected ? ui::to_imu32(p.accent)
                     : ImGui::IsItemHovered() ? ui::to_imu32(p.pressed)
                                              : ui::to_imu32(p.hover);
    dl->AddRectFilled(pos, {pos.x + width, pos.y + height}, bg, 6);
    const ImVec2 text = ImGui::CalcTextSize(label);
    dl->AddText({std::round(pos.x + ((width - text.x) / 2)), std::round(pos.y + ((height - text.y) / 2))},
                ui::to_imu32(selected ? p.accent_text : p.text), label);
    return clicked;
}

// Text field bound to a config value: shows the value unless being edited,
// returns true on Enter.
bool text_field(const char* id, const char* hint, std::array<char, 256>& buffer, const std::string& value) {
    if (ImGui::GetActiveID() != ImGui::GetID(id)) {
        std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
    }
    return ImGui::InputTextWithHint(id, hint, buffer.data(), buffer.size(),
                                    ImGuiInputTextFlags_EnterReturnsTrue);
}

} // namespace

// --- Appearance --------------------------------------------------------------------------

void Settings::page_appearance() {
    const auto& c = app_.config();
    const auto& p = app_.palette();

    begin_card("Style");
    row("Mode");
    {
        const float w = (control_width() - 8) / 2;
        if (segment(p, "Dark", c.theme.dark, w) && !c.theme.dark) {
            save([](config::Document& d) { return d.set({"theme", "dark"}, true); });
        }
        ImGui::SameLine(0, 8);
        if (segment(p, "Light", !c.theme.dark, w) && c.theme.dark) {
            save([](config::Document& d) { return d.set({"theme", "dark"}, false); });
        }
    }
    row("Accent colour", "Selections, the focused window, switches.");
    for (const auto& swatch : kAccents) {
        const Color color = *Color::parse(swatch.hex);
        if (color_swatch(swatch.name, color, color == c.theme.accent, 32)) {
            save([hex = std::string(swatch.hex)](config::Document& d) { return d.set({"theme", "accent"}, hex); });
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", swatch.name);
        }
        ImGui::SameLine(0, 4);
    }
    ImGui::NewLine();
    row("Custom accent", "A colour as #rrggbb; Enter applies it.");
    if (text_field("##accent", "#4c8dff", accent_buffer_, c.theme.accent.hex())) {
        if (auto color = Color::parse(accent_buffer_.data())) {
            save([hex = color->hex()](config::Document& d) { return d.set({"theme", "accent"}, hex); });
        } else {
            error_ = std::format("\"{}\" is not a colour (#rgb, #rrggbb or #rrggbbaa)", accent_buffer_.data());
        }
    }
    end_card();

    begin_card("Text");
    row("Font", "Bar, apps and window titles.");
    if (ImGui::BeginCombo("##font", c.theme.font.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsWindowAppearing()) {
            filter_buffer_.fill('\0');
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##filter", "Search fonts", filter_buffer_.data(), filter_buffer_.size());
        for (const auto& family : font_families_) {
            if (!contains_ci(family, filter_buffer_.data())) {
                continue;
            }
            if (ImGui::Selectable(family.c_str(), family == c.theme.font)) {
                save([family](config::Document& d) { return d.set({"theme", "font"}, family); });
            }
        }
        ImGui::EndCombo();
    }
    row("Size");
    {
        float size = static_cast<float>(font_size_.value_or(c.theme.font_size));
        if (ImGui::SliderFloat("##fontsize", &size, 10, 20, "%.0f px")) {
            font_size_ = std::round(size);
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && font_size_) {
            save([v = *font_size_](config::Document& d) { return d.set({"theme", "font_size"}, v); });
            font_size_.reset();
        }
    }
    end_card();

    begin_card("Motion");
    row("Animations", "Windows fade in and out, panels slide. Also tells GTK apps.");
    if (toggle("##animations", c.theme.animations)) {
        save([v = !c.theme.animations](config::Document& d) { return d.set({"theme", "animations"}, v); });
    }
    end_card();

    begin_card("Background");
    {
        using Wallpaper = config::Desktop::Wallpaper;
        struct Kind {
            Wallpaper value;
            const char* key;
            const char* label;
        };
        static constexpr std::array kKinds{
            Kind{Wallpaper::Logo, "logo", "Logo"},
            Kind{Wallpaper::Picture, "picture", "Picture"},
            Kind{Wallpaper::Color, "color", "Colour"},
        };
        for (const auto& kind : kKinds) {
            if (wallpaper_tile(kind.key, kind.label, kind.value, kind.value == c.desktop.wallpaper)) {
                if (kind.value == Wallpaper::Picture && c.desktop.picture.empty()) {
                    choose_picture(); // nothing to show yet
                } else {
                    save([key = kind.key](config::Document& d) { return d.set({"desktop", "wallpaper"}, key); });
                }
            }
            ImGui::SameLine(0, 12);
        }
        ImGui::NewLine();

        if (c.desktop.wallpaper == Wallpaper::Picture) {
            const std::string where = c.desktop.picture.empty() ? std::string("None chosen yet.") : c.desktop.picture;
            row("Picture", where.c_str());
            if (ImGui::Button("Choose\u2026", {control_width(), 0})) {
                choose_picture();
            }
            row("Fit");
            using Fit = config::Desktop::Fit;
            struct Choice {
                Fit value;
                const char* key;
                const char* label;
            };
            static constexpr std::array kFits{
                Choice{Fit::Fill, "fill", "Fill the screen"},
                Choice{Fit::Fit, "fit", "Whole picture"},
                Choice{Fit::Center, "center", "Original size"},
            };
            const auto current = std::ranges::find(kFits, c.desktop.picture_fit, &Choice::value);
            if (ImGui::BeginCombo("##fit", current->label)) {
                for (const auto& choice : kFits) {
                    if (ImGui::Selectable(choice.label, choice.value == c.desktop.picture_fit)) {
                        save([key = choice.key](config::Document& d) {
                            return d.set({"desktop", "picture_fit"}, key);
                        });
                    }
                }
                ImGui::EndCombo();
            }
        }

        const char* hint = c.desktop.wallpaper == Wallpaper::Logo      ? "Behind the logo."
                           : c.desktop.wallpaper == Wallpaper::Picture ? "Around a picture that does not fill the screen."
                                                                       : nullptr;
        row("Colour", hint);
        for (const auto& swatch : kBackgrounds) {
            const Color color = *Color::parse(swatch.hex);
            if (color_swatch(swatch.name, color, color == c.desktop.background, 32)) {
                save([hex = std::string(swatch.hex)](config::Document& d) {
                    return d.set({"desktop", "background"}, hex);
                });
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", swatch.name);
            }
            ImGui::SameLine(0, 4);
        }
        ImGui::NewLine();
        row("Custom colour", "A colour as #rrggbb; Enter applies it.");
        if (text_field("##background", "#1b1d22", background_buffer_, c.desktop.background.hex())) {
            if (auto color = Color::parse(background_buffer_.data())) {
                save([hex = color->hex()](config::Document& d) { return d.set({"desktop", "background"}, hex); });
            } else {
                error_ = std::format("\"{}\" is not a colour", background_buffer_.data());
            }
        }
    }
    end_card();

    begin_card("Windows");
    row("Title bar height");
    {
        int height = titlebar_height_.value_or(c.windows.titlebar_height);
        if (ImGui::SliderInt("##titlebar", &height, 24, 48, "%d px")) {
            titlebar_height_ = height;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && titlebar_height_) {
            save([v = *titlebar_height_](config::Document& d) { return d.set({"windows", "titlebar_height"}, v); });
            titlebar_height_.reset();
        }
    }
    row("Border width");
    {
        int width = border_width_.value_or(c.windows.border_width);
        if (ImGui::SliderInt("##border", &width, 0, 4, "%d px")) {
            border_width_ = width;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && border_width_) {
            save([v = *border_width_](config::Document& d) { return d.set({"windows", "border_width"}, v); });
            border_width_.reset();
        }
    }
    end_card();
}

// --- Taskbar -----------------------------------------------------------------------------

bool Settings::wallpaper_tile(const char* id, const char* label, config::Desktop::Wallpaper kind, bool selected) {
    constexpr float kWidth = 128;
    constexpr float kHeight = 80;
    const auto& p = app_.palette();
    const auto& c = app_.config();
    const float label_height = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, {kWidth, kHeight + label_height + 4});
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end{start.x + kWidth, start.y + kHeight};
    // The desktop in miniature: the colour, and the logo or a picture sign.
    dl->AddRectFilled(start, end, ui::to_imu32(c.desktop.background), 6);
    const ImVec2 center{(start.x + end.x) / 2, (start.y + end.y) / 2};
    const float lum = (0.2126F * c.desktop.background.r) + (0.7152F * c.desktop.background.g) +
                      (0.0722F * c.desktop.background.b);
    const Color ink = lum > 0.5F ? Color{0, 0, 0, 0.45F} : Color{1, 1, 1, 0.55F};
    if (kind == config::Desktop::Wallpaper::Logo) {
        ui::draw_mark(dl, {center.x, center.y + 2}, 36, ui::to_imu32(p.accent));
    } else if (kind == config::Desktop::Wallpaper::Picture) {
        // A landscape: a sun over two hills.
        const ImU32 color = ui::to_imu32(ink);
        dl->AddCircleFilled({center.x + 14, center.y - 12}, 5, color);
        dl->AddTriangleFilled({center.x - 30, center.y + 18}, {center.x - 8, center.y - 6}, {center.x + 14, center.y + 18},
                              color);
        dl->AddTriangleFilled({center.x, center.y + 18}, {center.x + 16, center.y + 2}, {center.x + 32, center.y + 18},
                              color);
    }
    dl->AddRect(start, end, ui::to_imu32(selected ? p.accent : hovered ? p.text_dim : p.separator), 6,
                selected ? 2.0F : 1.0F);
    const ImVec2 text = ImGui::CalcTextSize(label);
    dl->AddText({start.x + std::max(0.0F, (kWidth - text.x) / 2), end.y + 4},
                ui::to_imu32(selected ? p.text : p.text_dim), label);
    return clicked;
}

void Settings::choose_picture() {
    if (picker_) {
        return; // already open
    }
    if (!mime_) {
        mime_ = std::make_unique<portal::MimeGlobs>();
        mime_->load("/usr/share/mime");
    }
    portal::DialogOptions options;
    options.mode = portal::DialogOptions::Mode::Open;
    options.title = "Choose a Background Picture";
    options.accept_label = "Choose";
    options.filters = {portal::Filter{.name = "Pictures", .rules = {{1, "image/*"}}},
                       portal::Filter{.name = "All files", .rules = {}}};
    const char* home = std::getenv("HOME");
    const fs::path home_dir = home != nullptr ? fs::path(home) : fs::path("/");
    const std::string& current = app_.config().desktop.picture;
    if (!current.empty()) {
        options.folder = fs::path(current).parent_path();
    } else {
        // The Pictures folder: $XDG_PICTURES_DIR, else user-dirs.dirs.
        fs::path pictures;
        if (const char* env = std::getenv("XDG_PICTURES_DIR"); env != nullptr && *env != 0) {
            pictures = env;
        } else {
            const char* config_home = std::getenv("XDG_CONFIG_HOME");
            const auto dirs = portal::user_dirs(
                home_dir, config_home != nullptr && *config_home != 0 ? fs::path(config_home) : home_dir / ".config");
            if (const auto it = dirs.find("PICTURES"); it != dirs.end()) {
                pictures = it->second;
            }
        }
        std::error_code ec;
        options.folder = !pictures.empty() && fs::is_directory(pictures, ec) ? pictures : home_dir;
    }
    picker_ = std::make_unique<portal::FileDialog>(
        app_, *mime_, std::move(options), [this, home_dir](const portal::DialogResult& result) {
            if (result.accepted && !result.paths.empty()) {
                // Under the home directory as "~/...", as one would write it.
                std::string path = result.paths.front().string();
                const std::string prefix = home_dir.string() + "/";
                if (home_dir != "/" && path.starts_with(prefix)) {
                    path = "~/" + path.substr(prefix.size());
                }
                save([path](config::Document& d) -> std::expected<void, std::string> {
                    if (auto set = d.set({"desktop", "picture"}, path); !set) {
                        return set;
                    }
                    return d.set({"desktop", "wallpaper"}, "picture");
                });
            }
            // Not from inside the dialog's own callback.
            app_.add_timer(std::chrono::milliseconds(0), [this] { picker_.reset(); });
        });
}

bool Settings::icon_button(const char* id, const char* icon_name, bool enabled) {
    const auto& p = app_.palette();
    const float size = ImGui::GetFrameHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton(id, {size, size});
    ImGui::EndDisabled();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (enabled && ImGui::IsItemHovered()) {
        dl->AddRectFilled(pos, {pos.x + size, pos.y + size}, ui::to_imu32(p.hover), 6);
    }
    const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
    if (const ui::Icon* icon = icons_.get(icon_name, static_cast<int>(16 * scale))) {
        const float pad = std::round((size - 16) / 2);
        dl->AddImage(icon->ref(), {pos.x + pad, pos.y + pad}, {pos.x + pad + 16, pos.y + pad + 16}, {0, 0},
                     {1, 1}, ui::to_imu32(enabled ? p.text : p.text_dim.with_alpha(0.4F)));
    }
    return clicked && enabled;
}

void Settings::page_taskbar() {
    const auto& c = app_.config();
    const auto& pins = c.taskbar.pinned;
    auto save_pins = [this](std::vector<std::string> list) {
        save([list = std::move(list)](config::Document& d) { return d.set({"taskbar", "pinned"}, list); });
    };

    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(app_.palette().text_dim));
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted("Pinned apps keep their slot: Super+F1, F2, ... starts them or brings them "
                           "forward. Other apps take the free slots; one slot always stays free.");
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();

    begin_card("Pinned apps");
    if (pins.empty()) {
        row("Nothing pinned yet", "Add apps below.");
        ImGui::Dummy({0, 0});
    }
    const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
    for (std::size_t i = 0; i < pins.size(); ++i) {
        const DesktopEntry* entry = applications_.find(pins[i]);
        const std::string label = std::format("F{}   {}", i + 1, entry != nullptr ? entry->name : pins[i]);
        ImGui::PushID(static_cast<int>(i));
        row(label.c_str(), pins[i].c_str());
        // Icon at the left of the control column, then the buttons.
        const float x = ImGui::GetCursorPosX();
        if (entry != nullptr) {
            if (const ui::Icon* icon = icons_.get(entry->icon, static_cast<int>(24 * scale))) {
                const ImVec2 at = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddImage(icon->ref(), {at.x, at.y}, {at.x + 24, at.y + 24});
            }
        }
        const float button = ImGui::GetFrameHeight();
        ImGui::SetCursorPosX(x + control_width() - (3 * button) - 8);
        if (icon_button("##up", "go-up-symbolic", i > 0)) {
            auto list = pins;
            std::swap(list[i], list[i - 1]);
            save_pins(std::move(list));
        }
        ImGui::SameLine(0, 4);
        if (icon_button("##down", "go-down-symbolic", i + 1 < pins.size())) {
            auto list = pins;
            std::swap(list[i], list[i + 1]);
            save_pins(std::move(list));
        }
        ImGui::SameLine(0, 4);
        if (icon_button("##remove", "list-remove-symbolic", true)) {
            auto list = pins;
            list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
            save_pins(std::move(list));
        }
        ImGui::PopID();
    }
    end_card();

    begin_card(nullptr);
    const bool full = pins.size() >= config::Taskbar::kMaxPins;
    row("Pin an app", full ? "All 11 pin slots are used." : "Takes the next free F-key.");
    ImGui::BeginDisabled(full);
    if (ImGui::BeginCombo("##pin", "Choose an app", ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsWindowAppearing()) {
            filter_buffer_.fill('\0');
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##filter", "Search apps", filter_buffer_.data(), filter_buffer_.size());
        for (const DesktopEntry* app : applications_.visible()) {
            if (std::ranges::contains(pins, app->id) || !contains_ci(app->name, filter_buffer_.data())) {
                continue;
            }
            if (ImGui::Selectable(std::format("{}##{}", app->name, app->id).c_str())) {
                auto list = pins;
                list.push_back(app->id);
                save_pins(std::move(list));
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    end_card();
}

// --- Keyboard ----------------------------------------------------------------------------

void Settings::page_keyboard() {
    const auto& c = app_.config();
    const auto& kb = c.keyboard;

    begin_card("Layout");
    row("Layout");
    const Xkb::Entry* layout = xkb_.layout(kb.layout);
    const std::string preview = layout != nullptr ? layout->description : kb.layout;
    if (ImGui::BeginCombo("##layout", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsWindowAppearing()) {
            filter_buffer_.fill('\0');
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##filter", "Search layouts", filter_buffer_.data(), filter_buffer_.size());
        for (const auto& entry : xkb_.layouts()) {
            if (!contains_ci(entry.description, filter_buffer_.data()) &&
                !contains_ci(entry.name, filter_buffer_.data())) {
                continue;
            }
            if (ImGui::Selectable(std::format("{}##{}", entry.description, entry.name).c_str(),
                                  entry.name == kb.layout)) {
                save([name = entry.name](config::Document& d) -> std::expected<void, std::string> {
                    if (auto r = d.set({"keyboard", "layout"}, name); !r) {
                        return r;
                    }
                    return d.erase({"keyboard", "variant"}); // variants belong to a layout
                });
            }
        }
        ImGui::EndCombo();
    }
    row("Variant");
    const auto variants = xkb_.variants(kb.layout);
    std::string variant_label = "Default";
    for (const auto* v : variants) {
        if (v->name == kb.variant) {
            variant_label = v->description;
        }
    }
    ImGui::BeginDisabled(variants.empty());
    if (ImGui::BeginCombo("##variant", variant_label.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable("Default", kb.variant.empty())) {
            save([](config::Document& d) { return d.erase({"keyboard", "variant"}); });
        }
        for (const auto* v : variants) {
            if (ImGui::Selectable(std::format("{}##{}", v->description, v->name).c_str(), v->name == kb.variant)) {
                save([name = v->name](config::Document& d) { return d.set({"keyboard", "variant"}, name); });
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    // Two common options as switches; the rest stays in the file.
    auto option_switch = [&](const char* id, const char* label, const char* hint, const std::string& option) {
        auto options = split_options(kb.options);
        const bool on = std::ranges::contains(options, option);
        row(label, hint);
        if (toggle(id, on)) {
            if (on) {
                std::erase(options, option);
            } else {
                options.push_back(option);
            }
            save([value = join(options, ",")](config::Document& d) {
                return value.empty() ? d.erase({"keyboard", "options"}) : d.set({"keyboard", "options"}, value);
            });
        }
    };
    option_switch("##nocaps", "Caps Lock is another Ctrl", nullptr, "ctrl:nocaps");
    option_switch("##compose", "Compose key on Right Alt",
                  "Right Alt then ' then e types é, and so on.", "compose:ralt");
    end_card();

    begin_card("Typing");
    row("Repeat delay", "How long a key is held before it repeats.");
    {
        int delay = repeat_delay_.value_or(kb.repeat_delay);
        if (ImGui::SliderInt("##delay", &delay, 150, 1000, "%d ms")) {
            repeat_delay_ = delay;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && repeat_delay_) {
            save([v = *repeat_delay_](config::Document& d) { return d.set({"keyboard", "repeat_delay"}, v); });
            repeat_delay_.reset();
        }
    }
    row("Repeat rate");
    {
        int rate = repeat_rate_.value_or(kb.repeat_rate);
        if (ImGui::SliderInt("##rate", &rate, 5, 60, "%d per second")) {
            repeat_rate_ = rate;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && repeat_rate_) {
            save([v = *repeat_rate_](config::Document& d) { return d.set({"keyboard", "repeat_rate"}, v); });
            repeat_rate_.reset();
        }
    }
    row("Try it");
    ImGui::InputTextWithHint("##test", "Type here", test_buffer_.data(), test_buffer_.size());
    end_card();
}

// --- Mouse & touchpad --------------------------------------------------------------------

void Settings::speed_slider(const char* id, const char* section, double value, std::optional<double>& editing) {
    auto speed = static_cast<float>(editing.value_or(value));
    // libinput's own scale: 0 is the device's normal speed.
    const char* format = std::abs(speed) < 0.05F ? "Normal" : (speed < 0 ? "Slower (%.1f)" : "Faster (+%.1f)");
    if (ImGui::SliderFloat(id, &speed, -1, 1, format)) {
        editing = std::round(static_cast<double>(speed) * 10) / 10; // steps of 0.1 keep the file tidy
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && editing) {
        save([section = std::string(section), v = *editing](config::Document& d) {
            return d.set({section, "speed"}, v);
        });
        editing.reset();
    }
}

void Settings::page_mouse() {
    const auto& c = app_.config();
    using Touchpad = config::Touchpad;
    auto set_bool = [this](const char* section, const char* key, bool value) {
        save([section, key, value](config::Document& d) { return d.set({section, key}, value); });
    };

    begin_card("Mouse");
    row("Pointer speed");
    speed_slider("##mousespeed", "mouse", c.mouse.speed, mouse_speed_);
    row("Acceleration", "Fast movements take the pointer further. Off, it moves as far as the mouse.");
    if (toggle("##accel", c.mouse.acceleration)) {
        set_bool("mouse", "acceleration", !c.mouse.acceleration);
    }
    row("Natural scrolling", "The wheel moves the content instead of the view, like a touchscreen.");
    if (toggle("##mousenatural", c.mouse.natural_scroll)) {
        set_bool("mouse", "natural_scroll", !c.mouse.natural_scroll);
    }
    row("Primary button", "For mice and touchpads.");
    if (ImGui::BeginCombo("##primary", c.mouse.left_handed ? "Right" : "Left")) {
        for (const bool left_handed : {false, true}) {
            if (ImGui::Selectable(left_handed ? "Right" : "Left", left_handed == c.mouse.left_handed)) {
                set_bool("mouse", "left_handed", left_handed);
            }
        }
        ImGui::EndCombo();
    }
    end_card();

    const auto& tp = c.touchpad;
    begin_card("Touchpad");
    row("Pointer speed");
    speed_slider("##touchpadspeed", "touchpad", tp.speed, touchpad_speed_);
    row("Tap to click", "Tap with one finger to click, two to right-click, three to middle-click.");
    if (toggle("##tap", tp.tap_to_click)) {
        set_bool("touchpad", "tap_to_click", !tp.tap_to_click);
    }
    row("Natural scrolling", "The content follows your fingers.");
    if (toggle("##tpnatural", tp.natural_scroll)) {
        set_bool("touchpad", "natural_scroll", !tp.natural_scroll);
    }
    row("Scrolling");
    {
        struct Choice {
            Touchpad::Scroll value;
            const char* key;
            const char* label;
        };
        static constexpr std::array kScroll{
            Choice{Touchpad::Scroll::TwoFinger, "two_finger", "Two fingers"},
            Choice{Touchpad::Scroll::Edge, "edge", "Along the right edge"},
            Choice{Touchpad::Scroll::None, "none", "Off"},
        };
        const auto current = std::ranges::find(kScroll, tp.scroll, &Choice::value);
        if (ImGui::BeginCombo("##scroll", current->label)) {
            for (const auto& choice : kScroll) {
                if (ImGui::Selectable(choice.label, choice.value == tp.scroll)) {
                    save([key = choice.key](config::Document& d) { return d.set({"touchpad", "scroll"}, key); });
                }
            }
            ImGui::EndCombo();
        }
    }
    row("Right click", "Pressing the pad down: where, or with how many fingers.");
    {
        struct Choice {
            std::optional<Touchpad::Click> value;
            const char* key; // null: remove the setting
            const char* label;
        };
        static const std::array kClick{
            Choice{std::nullopt, nullptr, "Touchpad default"},
            Choice{Touchpad::Click::Areas, "areas", "Bottom-right corner"},
            Choice{Touchpad::Click::Fingers, "fingers", "Two fingers"},
        };
        const auto current = std::ranges::find(kClick, tp.click, &Choice::value);
        if (ImGui::BeginCombo("##click", current->label)) {
            for (const auto& choice : kClick) {
                if (ImGui::Selectable(choice.label, choice.value == tp.click)) {
                    save([key = choice.key](config::Document& d) {
                        return key == nullptr ? d.erase({"touchpad", "click"}) : d.set({"touchpad", "click"}, key);
                    });
                }
            }
            ImGui::EndCombo();
        }
    }
    row("Disable while typing", "Ignores the touchpad for a moment after each key.");
    if (toggle("##dwt", tp.disable_while_typing)) {
        set_bool("touchpad", "disable_while_typing", !tp.disable_while_typing);
    }
    end_card();
}

// --- Lock & power ------------------------------------------------------------------------

void Settings::page_power() {
    const auto& idle = app_.config().idle;
    auto duration_combo = [&](const char* id, int current, const char* key) {
        if (ImGui::BeginCombo(id, duration_label(current).c_str())) {
            for (const auto& d : kLockTimes) {
                if (ImGui::Selectable(d.label, d.seconds == current)) {
                    save([key = std::string(key), v = d.seconds](config::Document& doc) {
                        return doc.set({"idle", key}, v);
                    });
                }
            }
            ImGui::EndCombo();
        }
    };

    begin_card("Lock");
    row("Lock the screen after", "Time without input. Apps playing video keep the session awake.");
    duration_combo("##lock", idle.lock_after, "lock_after");
    row("Lock before sleep", "Suspend and hibernate lock the screen first.");
    if (toggle("##sleep", idle.lock_before_sleep)) {
        save([v = !idle.lock_before_sleep](config::Document& d) { return d.set({"idle", "lock_before_sleep"}, v); });
    }
    row("Lock now", "Super+L does the same.");
    if (ImGui::Button("Lock", {control_width(), 0})) {
        spawn_detached({"loginctl", "lock-session"});
    }
    end_card();

    begin_card("Screen");
    row("Turn the screen off after", "On the lock screen it goes off after a minute at most.");
    duration_combo("##screen", idle.screen_off_after, "screen_off_after");
    end_card();
}

// --- Apps & startup ----------------------------------------------------------------------

void Settings::page_apps() {
    const auto& c = app_.config();

    begin_card("Default apps");
    row("Terminal", "Super+Return, and apps that run in a terminal.");
    std::vector<std::string> terminals;
    for (const char* t : kTerminals) {
        if (on_path(t)) {
            terminals.emplace_back(t);
        }
    }
    if (!std::ranges::contains(terminals, c.terminal)) {
        terminals.insert(terminals.begin(), c.terminal);
    }
    if (ImGui::BeginCombo("##terminal", c.terminal.c_str())) {
        for (const auto& t : terminals) {
            if (ImGui::Selectable(t.c_str(), t == c.terminal)) {
                save([t](config::Document& d) { return d.set({"terminal"}, t); });
            }
        }
        ImGui::EndCombo();
    }
    row("Terminal command", "Any command line, e.g. \"tetra-terminal --title Shell\"; Enter applies it.");
    if (text_field("##terminal-command", "tetra-terminal", terminal_buffer_, c.terminal) && terminal_buffer_[0] != '\0') {
        save([v = std::string(terminal_buffer_.data())](config::Document& d) { return d.set({"terminal"}, v); });
    }
    end_card();

    begin_card("Startup");
    for (std::size_t i = 0; i < c.autostart.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        row(c.autostart[i].c_str());
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + control_width() - ImGui::GetFrameHeight());
        if (icon_button("##remove", "list-remove-symbolic", true)) {
            auto list = c.autostart;
            list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
            save([list = std::move(list)](config::Document& d) { return d.set({"autostart"}, list); });
        }
        ImGui::PopID();
    }
    row("Run at startup", "A command started with tetra (from the next login); Enter adds it.");
    if (ImGui::InputTextWithHint("##add", "nm-applet --indicator", autostart_buffer_.data(),
                                 autostart_buffer_.size(), ImGuiInputTextFlags_EnterReturnsTrue) &&
        autostart_buffer_[0] != '\0') {
        auto list = c.autostart;
        list.emplace_back(autostart_buffer_.data());
        save([list = std::move(list)](config::Document& d) { return d.set({"autostart"}, list); });
        autostart_buffer_.fill('\0');
    }
    end_card();

    begin_card("Advanced");
    row("Shell", "The bar and its services; restarts when changed. Empty runs no shell.");
    if (text_field("##shell", "tetra-shell", shell_buffer_, c.shell)) {
        save([v = std::string(shell_buffer_.data())](config::Document& d) { return d.set({"shell"}, v); });
    }
    end_card();
}

// --- About -------------------------------------------------------------------------------

void Settings::page_about() {
    const auto path = config::active_file();
    begin_card(nullptr);
    row("tetra", "A small Wayland desktop.");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TETRA_VERSION);
    row("Configuration", path.c_str());
    if (ImGui::Button("Open in an editor", {control_width(), 0})) {
        spawn_detached({"xdg-open", path.string()});
    }
    utsname uts{};
    if (uname(&uts) == 0) {
        row("Kernel");
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(uts.release); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    }
    row("Display", "Wayland, with Xwayland for X11 apps.");
    const char* display = std::getenv("WAYLAND_DISPLAY"); // NOLINT(concurrency-mt-unsafe)
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(display != nullptr ? display : "-");
    end_card();
}

} // namespace tetra::settings
