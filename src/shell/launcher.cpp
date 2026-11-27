#include "launcher.hpp"

#include "bar.hpp"
#include "motion.hpp"
#include "shell.hpp"
#include "style.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>

namespace tetra::shell {

namespace {

constexpr float kPanelWidth = 620;
constexpr float kPanelHeight = 560;
constexpr int kColumns = 6;
constexpr float kCellHeight = 92;
constexpr float kIconSize = 40;

bool contains_ci(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    auto it = std::ranges::search(haystack, needle, [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return !it.empty();
}

bool matches(const DesktopEntry& e, std::string_view query) {
    return contains_ci(e.name, query) || contains_ci(e.generic_name, query) ||
           contains_ci(e.id, query) ||
           std::ranges::any_of(e.keywords, [&](const std::string& k) { return contains_ci(k, query); });
}

// Fits `text` into `width` pixels, ending with "..." if cut.
std::string ellipsize(const std::string& text, float width) {
    if (ImGui::CalcTextSize(text.c_str()).x <= width) {
        return text;
    }
    std::string out = text;
    while (!out.empty() && ImGui::CalcTextSize((out + "...").c_str()).x > width) {
        out.pop_back();
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) {
            out.pop_back(); // do not cut UTF-8 sequences
        }
        if (!out.empty() && (static_cast<unsigned char>(out.back()) & 0x80) != 0) {
            out.pop_back();
        }
    }
    return out + "...";
}

} // namespace

Launcher::Launcher(Shell& shell) : shell_(shell), all_(shell.applications().visible()) {
    matches_ = all_;
    window_ = std::make_unique<ui::LayerWindow>(
        shell_.app(), ui::LayerWindow::Options{
                          .layer = ui::LayerWindow::Layer::Overlay,
                          .anchor = ui::LayerWindow::kTop | ui::LayerWindow::kBottom |
                                    ui::LayerWindow::kLeft | ui::LayerWindow::kRight,
                          .width = 0,
                          .height = 0,
                          .exclusive_zone = -1, // cover the bar too; clicks outside close
                          .keyboard = ui::LayerWindow::Keyboard::Exclusive,
                          .name_space = "tetra-launcher",
                          .output = nullptr, // the output under the pointer
                      });
    window_->on_draw = [this] { draw(); };
    window_->on_closed = [this] { shell_.hide_launcher(); };
}

void Launcher::update_matches() {
    if (query_ == last_query_) {
        return;
    }
    last_query_ = query_;
    matches_.clear();
    for (const DesktopEntry* entry : all_) {
        if (matches(*entry, query_)) {
            matches_.push_back(entry);
        }
    }
    // Names starting with the query first.
    std::ranges::stable_sort(matches_, [&](const DesktopEntry* a, const DesktopEntry* b) {
        const bool pa = contains_ci(a->name.substr(0, query_.size()), query_);
        const bool pb = contains_ci(b->name.substr(0, query_.size()), query_);
        return pa && !pb;
    });
    selected_ = 0;
}

void Launcher::launch(const DesktopEntry& entry) {
    shell_.launch(entry);
    shell_.hide_launcher();
}

void Launcher::draw_power_menu(float right, float y) {
    const auto& p = shell_.app().palette();
    const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
    constexpr float kSmallIcon = 16;
    auto icon = [&](const char* name, ImVec2 at) {
        if (const ui::Icon* i = shell_.icons().get(name, static_cast<int>(kSmallIcon * scale))) {
            const ImU32 tint = i->symbolic ? ui::to_imu32(p.text) : IM_COL32_WHITE;
            ImGui::GetWindowDrawList()->AddImage(i->ref(), at, {at.x + kSmallIcon, at.y + kSmallIcon}, {0, 0},
                                                 {1, 1}, tint);
        }
    };

    // The button: an icon and "Power".
    const char* label = "Power";
    const float pad = ImGui::GetStyle().FramePadding.x;
    const float button_w = kSmallIcon + 8 + ImGui::CalcTextSize(label).x + (2 * pad);
    ImGui::SetCursorPos({right - button_w, y});
    const ImVec2 at = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##power", {button_w, ImGui::GetFrameHeight()})) {
        ImGui::OpenPopup("##powermenu");
    }
    const bool open = ImGui::IsPopupOpen("##powermenu");
    if (ImGui::IsItemHovered() || open) {
        ImGui::GetWindowDrawList()->AddRectFilled(at, {at.x + button_w, at.y + ImGui::GetFrameHeight()},
                                                  ui::to_imu32(p.hover), 6);
    }
    const float mid = at.y + (ImGui::GetFrameHeight() / 2);
    icon("system-shutdown-symbolic", {at.x + pad, std::round(mid - (kSmallIcon / 2))});
    ImGui::GetWindowDrawList()->AddText({at.x + pad + kSmallIcon + 8, std::round(mid - (ImGui::GetFontSize() / 2))},
                                        ui::to_imu32(p.text), label);

    struct Item {
        const char* icon;
        const char* label;
        std::optional<ui::Power::Action> action; // none: lock or log out
        bool log_out = false;
    };
    using Action = ui::Power::Action;
    const std::array<Item, 5> items{{
        {"system-lock-screen-symbolic", "Lock", std::nullopt, false},
        {"system-log-out-symbolic", "Log out", std::nullopt, true},
        {"weather-clear-night-symbolic", "Suspend", Action::Suspend, false},
        {"system-reboot-symbolic", "Restart", Action::Reboot, false},
        {"system-shutdown-symbolic", "Shut down", Action::PowerOff, false},
    }};
    int shown = 0;
    for (const Item& item : items) {
        shown += !item.action || shell_.power().can(*item.action) ? 1 : 0;
    }
    // Above the button, its right edge on the button's.
    const float row = ImGui::GetFrameHeight() + 4;
    const float menu_w = 180;
    const float menu_h = (static_cast<float>(shown) * row) + (2 * 6);
    ImGui::SetNextWindowPos({at.x + button_w - menu_w, at.y - menu_h - 6});
    ImGui::SetNextWindowSize({menu_w, menu_h});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {6, 6});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 4});
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, {0, 0.5F});
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ui::to_imvec4(p.window));
    if (ImGui::BeginPopup("##powermenu")) {
        for (const Item& item : items) {
            if (item.action && !shell_.power().can(*item.action)) {
                continue;
            }
            const ImVec2 start = ImGui::GetCursorScreenPos();
            const std::string text = std::string("         ") + item.label;
            if (ImGui::Selectable(text.c_str(), false, ImGuiSelectableFlags_None, {0, ImGui::GetFrameHeight()})) {
                if (item.action) {
                    shell_.power().request(*item.action);
                } else if (item.log_out) {
                    shell_.log_out();
                } else {
                    shell_.lock();
                }
                shell_.hide_launcher();
            }
            icon(item.icon, {start.x + 8, std::round(start.y + ((ImGui::GetFrameHeight() - kSmallIcon) / 2))});
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void Launcher::draw() {
    const auto& p = shell_.app().palette();
    // Rises from the bar while fading in.
    const float shown = ui::transition(shell_.app(), *window_, opened_, std::chrono::milliseconds(180));
    window_->set_fade(shown, 0, std::round((1 - shown) * 24));
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float width = std::min(kPanelWidth, viewport->Size.x - 24);
    const float height = std::min(kPanelHeight, viewport->Size.y - Bar::kHeight - 36);
    const ImVec2 pos{std::round((viewport->Size.x - width) / 2),
                     std::round(viewport->Size.y - Bar::kHeight - 12 - height)};

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize({width, height});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16, 16});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::to_imvec4(p.surface));
    ImGui::Begin("##launcher", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

    // Search field, focused from the start.
    if (first_frame_) {
        ImGui::SetKeyboardFocusHere();
        first_frame_ = false;
    }
    std::array<char, 256> buffer{};
    query_.copy(buffer.data(), buffer.size() - 1);
    ImGui::SetNextItemWidth(-1);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {12, 9});
    const bool enter = ImGui::InputTextWithHint("##search", "Type to search apps", buffer.data(),
                                                buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopStyleVar();
    query_ = buffer.data();
    update_matches();

    // Keyboard selection; the search field keeps focus while typing.
    const int count = static_cast<int>(matches_.size());
    const int before = selected_;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) {
        selected_ = std::min(selected_ + 1, count - 1);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) {
        selected_ = std::max(selected_ - 1, 0);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
        selected_ = std::min(selected_ + kColumns, count - 1);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
        selected_ = std::max(selected_ - kColumns, 0);
    }
    const bool scroll_to_selection = selected_ != before;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        shell_.hide_launcher();
    }
    if (enter && selected_ < count) {
        launch(*matches_.at(static_cast<std::size_t>(selected_)));
    }

    ImGui::Dummy({0, 6});
    const float footer = ImGui::GetFrameHeight() + 14;
    ImGui::BeginChild("##apps", {0, -footer}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cell_width = std::floor(ImGui::GetContentRegionAvail().x / kColumns);
    const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
    for (int i = 0; i < count; ++i) {
        const DesktopEntry& entry = *matches_.at(static_cast<std::size_t>(i));
        if (i % kColumns != 0) {
            ImGui::SameLine(0, 0);
        }
        ImGui::PushID(i);
        const ImVec2 cell = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##app", {cell_width, kCellHeight})) {
            launch(entry);
        }
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) {
            selected_ = i;
        }
        if (i == selected_ && scroll_to_selection) {
            ImGui::SetScrollHereY(0.5F);
        }
        if (hovered || i == selected_) {
            dl->AddRectFilled(cell, {cell.x + cell_width, cell.y + kCellHeight},
                              ui::to_imu32(p.hover), 8);
        }

        const ImVec2 icon_pos{std::round(cell.x + ((cell_width - kIconSize) / 2)), cell.y + 12};
        const ui::Icon* icon = shell_.icons().get(entry.icon, static_cast<int>(kIconSize * scale));
        if (icon == nullptr) {
            icon = shell_.icons().get("application-x-executable", static_cast<int>(kIconSize * scale));
        }
        if (icon != nullptr) {
            dl->AddImage(icon->ref(), icon_pos, {icon_pos.x + kIconSize, icon_pos.y + kIconSize});
        }
        const std::string label = ellipsize(entry.name, cell_width - 10);
        const ImVec2 size = ImGui::CalcTextSize(label.c_str());
        dl->AddText({std::round(cell.x + ((cell_width - size.x) / 2)), cell.y + 12 + kIconSize + 10},
                    ui::to_imu32(p.text), label.c_str());
        ImGui::PopID();
    }
    if (count == 0) {
        ImGui::TextDisabled("No apps match \"%s\"", query_.c_str());
    }
    ImGui::EndChild();

    // Footer: a line, then the power button at the right.
    {
        const ImVec2 line = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine({line.x, line.y + 6}, {line.x + ImGui::GetContentRegionAvail().x, line.y + 6},
                                            ui::to_imu32(p.separator));
        draw_power_menu(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x, ImGui::GetCursorPosY() + 13);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    // A click outside the panel closes the launcher. Test the rectangle, not
    // IsWindowHovered(): that is false while the clicked app button is active.
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool inside = mouse.x >= pos.x && mouse.x < pos.x + width && mouse.y >= pos.y &&
                        mouse.y < pos.y + height;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !inside) {
        shell_.hide_launcher();
    }
}

} // namespace tetra::shell
