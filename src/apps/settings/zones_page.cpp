// Settings: snap zones (a layout per display, the gap) and window memory.

#include "settings.hpp"

#include "config.hpp"
#include "style.hpp"
#include "window_memory.hpp"
#include "zones.hpp"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <system_error>

namespace tetra::settings {

namespace {

constexpr float kThumbWidth = 132;
constexpr float kThumbHeight = 74;
constexpr float kThumbSpacing = 12;

} // namespace

// A grid of layout thumbnails; returns the layout clicked, if any.
std::optional<std::string> Settings::layout_picker(const char* id, const std::string& current) {
    const auto& p = app_.palette();
    const auto layouts = zones::all(app_.config());
    std::optional<std::string> chosen;
    ImGui::PushID(id);
    const float label_height = ImGui::GetTextLineHeightWithSpacing();
    const float available = ImGui::GetContentRegionAvail().x;
    const int per_row = std::max(1, static_cast<int>((available + kThumbSpacing) / (kThumbWidth + kThumbSpacing)));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (std::size_t i = 0; i < layouts.size(); ++i) {
        const zones::Layout& layout = layouts[i];
        if (i % static_cast<std::size_t>(per_row) != 0) {
            ImGui::SameLine(0, kThumbSpacing);
        }
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 start = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##layout", {kThumbWidth, kThumbHeight + label_height + 4})) {
            chosen = layout.name;
        }
        const bool selected = layout.name == current;
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) {
            ImGui::SetTooltip("%s", layout.name.c_str());
        }
        const ImVec2 end{start.x + kThumbWidth, start.y + kThumbHeight};
        dl->AddRectFilled(start, end, ui::to_imu32(hovered ? p.hover : p.window), 6);
        dl->AddRect(start, end, ui::to_imu32(selected ? p.accent : p.separator), 6, selected ? 2.0F : 1.0F);
        // The zones, as the display would show them.
        const auto boxes = zones::place(layout, {.x = 0, .y = 0, .width = static_cast<int>(kThumbWidth - 8),
                                                 .height = static_cast<int>(kThumbHeight - 8)},
                                        4);
        for (const auto& b : boxes) {
            const ImVec2 a{start.x + 4 + static_cast<float>(b.x), start.y + 4 + static_cast<float>(b.y)};
            const ImVec2 z{a.x + static_cast<float>(b.width), a.y + static_cast<float>(b.height)};
            auto fill = selected ? p.accent : p.text_dim;
            dl->AddRectFilled(a, z, ui::to_imu32(fill.with_alpha(selected ? 0.55F : 0.25F)), 3);
        }
        const ImVec2 text = ImGui::CalcTextSize(layout.label.c_str());
        dl->AddText({start.x + std::max(0.0F, (kThumbWidth - text.x) / 2), end.y + 4},
                    ui::to_imu32(selected ? p.text : p.text_dim), layout.label.c_str());
        ImGui::PopID();
    }
    ImGui::PopID();
    return chosen;
}

void Settings::page_zones() {
    const auto& c = app_.config();

    begin_card(nullptr);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(app_.palette().text_dim));
    ImGui::TextWrapped("Hold Shift while dragging a window: the zones of the display under the pointer "
                       "appear, and the window fills the one you drop it on (near the edge between "
                       "two zones, both). Super+Left and Super+Right move the focused window from zone "
                       "to zone.");
    ImGui::PopStyleColor();
    end_card();

    const auto& list = displays_.list();
    if (!displays_.available() || list.empty()) {
        begin_card("Layout");
        if (auto chosen = layout_picker("##all", c.windows.zones)) {
            save([name = *chosen](config::Document& d) { return d.set({"windows", "zones"}, name); });
        }
        end_card();
    } else {
        for (std::size_t i = 0; i < list.size(); ++i) {
            const Display& d = *list[i];
            const std::string title = list.size() > 1 ? std::format("{} · {} ({})", i + 1, d.title(), d.name)
                                                      : std::string("Layout");
            begin_card(title.c_str());
            if (auto chosen = layout_picker(d.name.c_str(), zones::layout_name(c, d.name))) {
                const std::string members = display_members(d.name, {{"zones", std::format("\"{}\"", *chosen)}});
                save([name = d.name, members](config::Document& doc) {
                    return doc.merge_element({"outputs"}, "name", name, members);
                });
            }
            end_card();
        }
    }

    begin_card("Options");
    row("Gap between zones");
    {
        int gap = zone_gap_.value_or(c.windows.zone_gap);
        if (ImGui::SliderInt("##gap", &gap, 0, 32, "%d px")) {
            zone_gap_ = gap;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && zone_gap_) {
            save([v = *zone_gap_](config::Document& d) { return d.set({"windows", "zone_gap"}, v); });
            zone_gap_.reset();
        }
    }
    row("Remember window positions", "Windows open where they were last, on each display.");
    if (toggle("##remember", c.windows.remember_positions)) {
        save([v = !c.windows.remember_positions](config::Document& d) {
            return d.set({"windows", "remember_positions"}, v);
        });
    }
    row("Forget remembered positions", "Every window opens in the middle of the screen again.");
    const auto state = windows::state_file();
    std::error_code ec;
    const bool remembered = std::filesystem::exists(state, ec);
    ImGui::BeginDisabled(!remembered);
    if (ImGui::Button(remembered ? "Forget" : "Nothing remembered", {control_width(), 0})) {
        if (!std::filesystem::remove(state, ec) && ec) {
            error_ = std::format("Cannot remove {}: {}", state.string(), ec.message());
        }
    }
    ImGui::EndDisabled();
    end_card();
}

} // namespace tetra::settings
