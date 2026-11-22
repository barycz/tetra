#include "bar.hpp"

#include "style.hpp"

#include <imgui.h>

#include <array>
#include <cmath>
#include <ctime>
#include <string>

namespace tetra::shell {

namespace {

ImVec2 lerp(ImVec2 a, ImVec2 b, float t) {
    return {a.x + ((b.x - a.x) * t), a.y + ((b.y - a.y) * t)};
}

// The tetra mark: a triangle split into four, the three corner triangles
// filled - a flattened tetrahedron.
void draw_logo(ImDrawList* dl, ImVec2 center, float size, ImU32 color) {
    const float h = size * 0.8660254F; // equilateral height
    const ImVec2 top{center.x, center.y - (h * 0.58F)};
    const ImVec2 left{center.x - (size / 2), center.y + (h * 0.42F)};
    const ImVec2 right{center.x + (size / 2), center.y + (h * 0.42F)};
    const ImVec2 mid_left = lerp(top, left, 0.5F);
    const ImVec2 mid_right = lerp(top, right, 0.5F);
    const ImVec2 mid_bottom = lerp(left, right, 0.5F);

    const std::array<std::array<ImVec2, 3>, 3> corners = {{
        {top, mid_left, mid_right},
        {mid_left, left, mid_bottom},
        {mid_right, mid_bottom, right},
    }};
    constexpr float kGap = 0.12F; // shrink towards each triangle's centre
    for (const auto& tri : corners) {
        const ImVec2 c{(tri[0].x + tri[1].x + tri[2].x) / 3, (tri[0].y + tri[1].y + tri[2].y) / 3};
        dl->AddTriangleFilled(lerp(tri[0], c, kGap), lerp(tri[1], c, kGap), lerp(tri[2], c, kGap),
                              color);
    }
}

std::string format_time(const char* format) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    std::array<char, 64> buffer{};
    const std::size_t len = std::strftime(buffer.data(), buffer.size(), format, &local);
    return {buffer.data(), len};
}

} // namespace

Bar::Bar(ui::App& app, const ui::OutputInfo& output) : app_(app) {
    window_ = std::make_unique<ui::LayerWindow>(
        app_, ui::LayerWindow::Options{
                  .layer = ui::LayerWindow::Layer::Top,
                  .anchor = ui::LayerWindow::kBottom | ui::LayerWindow::kLeft |
                            ui::LayerWindow::kRight,
                  .width = 0,
                  .height = kHeight,
                  .exclusive_zone = kHeight,
                  .keyboard = ui::LayerWindow::Keyboard::None,
                  .name_space = "tetra-bar",
                  .output = output.output,
              });
    window_->on_draw = [this] { draw(); };
}

void Bar::draw() {
    const auto& p = app_.palette();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::to_imvec4(p.surface));
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("##bar", nullptr, flags);

    const float width = viewport->Size.x;
    const float height = viewport->Size.y;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine({0, 0.5F}, {width, 0.5F}, ui::to_imu32(p.separator)); // top edge

    constexpr float kButton = 40;
    draw_launcher(std::round((width - kButton) / 2), std::round((height - kButton) / 2), kButton);
    draw_clock(width - 14, height);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void Bar::draw_launcher(float x, float y, float size) {
    const auto& p = app_.palette();
    ImGui::SetCursorPos({x, y});
    ImGui::InvisibleButton("##launcher", {size, size});
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (ImGui::IsItemClicked()) {
        // The launcher menu arrives in phase 2.
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 min{x, y};
    const ImVec2 max{x + size, y + size};
    if (hovered) {
        dl->AddRectFilled(min, max, ui::to_imu32(active ? p.pressed : p.hover), 8);
    }
    draw_logo(dl, {x + (size / 2), y + (size / 2) + 1}, size * 0.55F, ui::to_imu32(p.accent));
}

void Bar::draw_clock(float right, float height) {
    const auto& p = app_.palette();
    const std::string time = format_time("%H:%M");
    const std::string date = format_time("%x");

    const float font_size = ImGui::GetStyle().FontSizeBase;
    const float small_size = std::round(font_size * 0.86F);
    ImGui::PushFont(nullptr, font_size);
    const ImVec2 time_size = ImGui::CalcTextSize(time.c_str());
    ImGui::PopFont();
    ImGui::PushFont(nullptr, small_size);
    const ImVec2 date_size = ImGui::CalcTextSize(date.c_str());
    ImGui::PopFont();

    const float gap = 1;
    const float top = std::round((height - (time_size.y + gap + date_size.y)) / 2);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Right-aligned, like the Windows tray clock.
    ImGui::PushFont(nullptr, font_size);
    dl->AddText({std::round(right - time_size.x), top}, ui::to_imu32(p.text), time.c_str());
    ImGui::PopFont();
    ImGui::PushFont(nullptr, small_size);
    dl->AddText({std::round(right - date_size.x), top + time_size.y + gap}, ui::to_imu32(p.text_dim),
                date.c_str());
    ImGui::PopFont();
}

} // namespace tetra::shell
