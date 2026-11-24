#include "settings.hpp"

#include "config.hpp"
#include "style.hpp"

#include <fontconfig/fontconfig.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace tetra::settings {

namespace {

constexpr float kSidebarWidth = 224;
constexpr float kPagePadding = 28;
constexpr float kControlWidth = 300;
constexpr float kCardPadding = 18;
constexpr float kIconSize = 18;

std::vector<std::string> list_font_families() {
    std::set<std::string> families;
    FcPattern* pattern = FcPatternCreate();
    FcObjectSet* objects = FcObjectSetBuild(FC_FAMILY, FC_SCALABLE, nullptr);
    if (FcFontSet* fonts = FcFontList(nullptr, pattern, objects)) {
        for (int i = 0; i < fonts->nfont; ++i) {
            FcPattern* font = fonts->fonts[i]; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            FcBool scalable = FcFalse;
            FcChar8* family = nullptr;
            if (FcPatternGetBool(font, FC_SCALABLE, 0, &scalable) == FcResultMatch && scalable == FcTrue &&
                FcPatternGetString(font, FC_FAMILY, 0, &family) == FcResultMatch) {
                families.emplace(reinterpret_cast<const char*>(family)); // NOLINT
            }
        }
        FcFontSetDestroy(fonts);
    }
    FcObjectSetDestroy(objects);
    FcPatternDestroy(pattern);
    return {families.begin(), families.end()};
}

} // namespace

const std::vector<Settings::PageInfo>& Settings::pages() {
    static const std::vector<PageInfo> list = {
        {Page::Appearance, "appearance", "Appearance", "preferences-desktop-appearance-symbolic"},
        {Page::Taskbar, "taskbar", "Taskbar", "view-app-grid-symbolic"},
        {Page::Keyboard, "keyboard", "Keyboard", "input-keyboard-symbolic"},
        {Page::Power, "power", "Lock & power", "system-lock-screen-symbolic"},
        {Page::Apps, "apps", "Apps & startup", "application-x-executable-symbolic"},
        {Page::About, "about", "About", "help-about-symbolic"},
    };
    return list;
}

Settings::Settings(ui::App& app) : app_(app), icons_(app) {
    applications_.load();
    xkb_.load();
    font_families_ = list_font_families();
    if (auto loaded = config::load(config::active_file()); !loaded) {
        load_error_ = loaded.error();
    }

    window_ = std::make_unique<ui::ToplevelWindow>(
        app_, ui::ToplevelWindow::Options{.title = "Settings", .width = 980, .height = 680});
    window_->on_draw = [this] { draw(); };
    app_.on_config_changed = [this] {
        // A reload succeeded, so the file loads again.
        load_error_.clear();
        window_->request_redraw();
    };
}

void Settings::show_page(std::string_view id) {
    for (const auto& info : pages()) {
        if (id == info.id) {
            page_ = info.page;
        }
    }
    window_->request_redraw();
}

void Settings::save(const Edit& edit) {
    auto result = config::edit_file(config::active_file(), edit);
    error_ = result ? std::string() : result.error();
    window_->request_redraw();
}

void Settings::draw() {
    const auto& p = app_.palette();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::to_imvec4(p.window));
    ImGui::Begin("##settings", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar(3);

    draw_sidebar(kSidebarWidth, viewport->Size.y);

    ImGui::SetCursorPos({kSidebarWidth, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {kPagePadding, kPagePadding - 6});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::to_imvec4(p.window));
    ImGui::BeginChild("##page", {viewport->Size.x - kSidebarWidth, viewport->Size.y},
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    for (const auto& info : pages()) {
        if (info.page == page_) {
            ImGui::PushFont(nullptr, 24);
            ImGui::TextUnformatted(info.title);
            ImGui::PopFont();
        }
    }
    draw_status();
    switch (page_) {
    case Page::Appearance:
        page_appearance();
        break;
    case Page::Taskbar:
        page_taskbar();
        break;
    case Page::Keyboard:
        page_keyboard();
        break;
    case Page::Power:
        page_power();
        break;
    case Page::Apps:
        page_apps();
        break;
    case Page::About:
        page_about();
        break;
    }
    ImGui::Dummy({0, kPagePadding});
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImGui::End();
    ImGui::PopStyleColor();
}

void Settings::draw_sidebar(float width, float height) {
    const auto& p = app_.palette();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled({0, 0}, {width, height}, ui::to_imu32(p.surface));
    dl->AddLine({width - 0.5F, 0}, {width - 0.5F, height}, ui::to_imu32(p.separator));

    ImGui::SetCursorPos({20, 22});
    ImGui::PushFont(nullptr, 17);
    ImGui::TextUnformatted("Settings");
    ImGui::PopFont();

    const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
    float y = 64;
    for (const auto& info : pages()) {
        const bool selected = info.page == page_;
        ImGui::SetCursorPos({10, y});
        ImGui::PushID(info.id);
        if (ImGui::InvisibleButton("##page", {width - 20, 38})) {
            page_ = info.page;
            error_.clear();
            filter_buffer_.fill('\0');
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        if (selected || hovered) {
            dl->AddRectFilled({10, y}, {width - 10, y + 38},
                              ui::to_imu32(selected ? p.pressed : p.hover), 8);
        }
        if (selected) {
            dl->AddRectFilled({10, y + 10}, {13, y + 28}, ui::to_imu32(p.accent), 2);
        }
        if (const ui::Icon* icon = icons_.get(info.icon, static_cast<int>(kIconSize * scale))) {
            dl->AddImage(icon->ref(), {24, y + 10}, {24 + kIconSize, y + 10 + kIconSize}, {0, 0},
                         {1, 1}, ui::to_imu32(selected ? p.text : p.text_dim));
        }
        dl->AddText({54, y + 10}, ui::to_imu32(p.text), info.title);
        y += 42;
    }
}

void Settings::draw_status() {
    const auto& p = app_.palette();
    const std::string& message = !error_.empty() ? error_ : load_error_;
    if (message.empty()) {
        return;
    }
    ImGui::Dummy({0, 6});
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::Indent(14);
    ImGui::Dummy({0, 4});
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 28);
    ImGui::TextUnformatted(error_.empty() ? "config.json has an error, so Settings leaves it alone until it is fixed:"
                                          : "The change was not saved:");
    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.text_dim));
    ImGui::TextUnformatted(message.c_str());
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::Dummy({0, 4});
    ImGui::Unindent(14);
    const ImVec2 end{start.x + width, ImGui::GetCursorScreenPos().y};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRect(start, end, ui::to_imu32(p.close_hover), 8, 1.5F);
}

// --- layout helpers ----------------------------------------------------------------------

namespace {
struct Card {
    ImVec2 start;
    float width = 0;
    float left = 0; // local x where the card starts
};
Card g_card; // one card at a time
} // namespace

void Settings::begin_card(const char* title) {
    const auto& p = app_.palette();
    ImGui::Dummy({0, 12});
    if (title != nullptr) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.text_dim));
        ImGui::TextUnformatted(title);
        ImGui::PopStyleColor();
        ImGui::Dummy({0, 0});
    }
    g_card.start = ImGui::GetCursorScreenPos();
    g_card.width = ImGui::GetContentRegionAvail().x;
    g_card.left = ImGui::GetCursorPosX();
    ImGui::GetWindowDrawList()->ChannelsSplit(2);
    ImGui::GetWindowDrawList()->ChannelsSetCurrent(1);
    ImGui::Indent(kCardPadding);
    ImGui::Dummy({0, 4});
    card_rows_ = 0;
}

void Settings::end_card() {
    const auto& p = app_.palette();
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), row_bottom_));
    row_bottom_ = 0;
    ImGui::Dummy({0, 4});
    ImGui::Unindent(kCardPadding);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSetCurrent(0);
    const ImVec2 end{g_card.start.x + g_card.width, ImGui::GetCursorScreenPos().y};
    dl->AddRectFilled(g_card.start, end, ui::to_imu32(p.surface), 10);
    dl->AddRect(g_card.start, end, ui::to_imu32(p.separator), 10);
    dl->ChannelsMerge();
    ImGui::Dummy({0, 2});
}

float Settings::control_width() const {
    return std::min(kControlWidth, g_card.width * 0.45F);
}

void Settings::row(const char* label, const char* hint) {
    const auto& p = app_.palette();
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), row_bottom_));
    if (card_rows_++ > 0) {
        const float y = ImGui::GetCursorScreenPos().y + 2;
        ImGui::GetWindowDrawList()->AddLine({g_card.start.x + kCardPadding, y},
                                            {g_card.start.x + g_card.width - kCardPadding, y},
                                            ui::to_imu32(p.separator));
        ImGui::Dummy({0, 6});
    } else {
        ImGui::Dummy({0, 2});
    }
    const float top = ImGui::GetCursorPosY();
    const float label_width = g_card.width - control_width() - (3 * kCardPadding);
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + label_width);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (hint != nullptr) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.text_dim));
        ImGui::PushFont(nullptr, 13);
        ImGui::TextUnformatted(hint);
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    row_bottom_ = ImGui::GetCursorPosY(); // the next row starts below the label too
    // The control: right-aligned, at the label's first line.
    ImGui::SetCursorPos({g_card.left + g_card.width - kCardPadding - control_width(), top});
    ImGui::SetNextItemWidth(control_width());
}

bool Settings::toggle(const char* id, bool value) {
    const auto& p = app_.palette();
    const ImVec2 size{42, 24};
    // Right-aligned inside the control column.
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + control_width() - size.x);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 track = value ? ui::to_imu32(p.accent) : ui::to_imu32(p.separator);
    dl->AddRectFilled(pos, {pos.x + size.x, pos.y + size.y}, track, size.y / 2);
    const float knob = size.y / 2 - 3;
    const float cx = value ? pos.x + size.x - knob - 3 : pos.x + knob + 3;
    dl->AddCircleFilled({cx, pos.y + (size.y / 2)}, knob,
                        value ? ui::to_imu32(p.accent_text) : ui::to_imu32(p.text));
    return clicked;
}

bool Settings::color_swatch(const char* id, const config::Color& color, bool selected, float size) {
    const auto& p = app_.palette();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, {size, size});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 center{pos.x + (size / 2), pos.y + (size / 2)};
    dl->AddCircleFilled(center, (size / 2) - 3, ui::to_imu32(color));
    dl->AddCircle(center, (size / 2) - 3, ui::to_imu32(p.separator), 0, 1); // dark swatches on dark cards
    if (selected) {
        dl->AddCircle(center, (size / 2) - 0.5F, ui::to_imu32(p.text), 0, 2);
    } else if (ImGui::IsItemHovered()) {
        dl->AddCircle(center, (size / 2) - 0.5F, ui::to_imu32(p.text_dim), 0, 1);
    }
    return clicked;
}

} // namespace tetra::settings
