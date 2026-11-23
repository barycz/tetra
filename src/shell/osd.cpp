#include "osd.hpp"

#include "shell.hpp"
#include "style.hpp"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>

namespace tetra::shell {

namespace {

constexpr int kWidth = 300;
constexpr int kHeight = 56;
constexpr int kAboveBar = 24;
constexpr auto kVisibleFor = std::chrono::milliseconds(1500);
constexpr float kIconSize = 22;

} // namespace

Osd::Osd(Shell& shell) : shell_(shell) {}

Osd::~Osd() {
    if (hide_timer_) {
        shell_.app().cancel_timer(*hide_timer_);
    }
}

void Osd::show(std::string icon, float level, bool dimmed) {
    icon_ = std::move(icon);
    level_ = std::clamp(level, 0.0F, 1.0F);
    dimmed_ = dimmed;
    if (!window_) {
        ui::LayerWindow::Options options{
            .layer = ui::LayerWindow::Layer::Overlay,
            .anchor = ui::LayerWindow::kBottom,
            .width = kWidth,
            .height = kHeight,
            .exclusive_zone = 0, // stays inside the usable area, i.e. above the bar
            .keyboard = ui::LayerWindow::Keyboard::None,
            .name_space = "tetra-osd",
            .output = nullptr,
        };
        options.margin.bottom = kAboveBar;
        window_ = std::make_shared<ui::LayerWindow>(shell_.app(), options);
        window_->on_draw = [this] { draw(); };
    }
    window_->request_redraw();

    if (hide_timer_) {
        shell_.app().cancel_timer(*hide_timer_);
    }
    hide_timer_ = shell_.app().add_timer(kVisibleFor, [this] {
        hide_timer_.reset();
        hide();
    });
}

void Osd::hide() {
    if (window_) {
        std::shared_ptr<ui::LayerWindow> closing = std::move(window_);
        shell_.app().add_timer(std::chrono::milliseconds(0), [closing] {});
    }
}

void Osd::draw() {
    const auto& p = shell_.app().palette();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::Begin("##osd", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = viewport->Size.x;
    const float h = viewport->Size.y;
    dl->AddRectFilled({0, 0}, {w, h}, ui::to_imu32(p.surface), 12);
    dl->AddRect({0.5F, 0.5F}, {w - 0.5F, h - 0.5F}, ui::to_imu32(p.separator), 12);

    // Icon
    const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
    const float icon_y = std::round((h - kIconSize) / 2);
    if (const ui::Icon* icon = shell_.icons().get(icon_, static_cast<int>(kIconSize * scale))) {
        const ImU32 tint = icon->symbolic ? ui::to_imu32(p.text) : IM_COL32_WHITE;
        dl->AddImage(icon->ref(), {18, icon_y}, {18 + kIconSize, icon_y + kIconSize}, {0, 0}, {1, 1}, tint);
    }

    // Level bar and percentage
    const std::string percent = std::format("{}", static_cast<int>(std::lround(level_ * 100)));
    const ImVec2 text_size = ImGui::CalcTextSize("100");
    const float bar_left = 18 + kIconSize + 16;
    const float bar_right = w - 18 - text_size.x - 12;
    const float bar_y = std::round(h / 2);
    dl->AddRectFilled({bar_left, bar_y - 2}, {bar_right, bar_y + 2}, ui::to_imu32(p.hover), 2);
    const float fill = bar_left + ((bar_right - bar_left) * level_);
    dl->AddRectFilled({bar_left, bar_y - 2}, {fill, bar_y + 2},
                      ui::to_imu32(dimmed_ ? p.text_dim : p.accent), 2);
    const ImVec2 size = ImGui::CalcTextSize(percent.c_str());
    dl->AddText({w - 18 - size.x, std::round((h - size.y) / 2)},
                ui::to_imu32(dimmed_ ? p.text_dim : p.text), percent.c_str());
    ImGui::End();
}

} // namespace tetra::shell
