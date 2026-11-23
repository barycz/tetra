#include "volume_popup.hpp"

#include "bar.hpp"
#include "shell.hpp"
#include "style.hpp"

#include <imgui.h>

#include <cmath>

namespace tetra::shell {

namespace {

constexpr float kPanelWidth = 340;
constexpr float kPanelHeight = 112;
constexpr float kMargin = 12;
constexpr float kIconSize = 20;

} // namespace

VolumePopup::VolumePopup(Shell& shell) : shell_(shell) {
    window_ = std::make_unique<ui::LayerWindow>(
        shell_.app(), ui::LayerWindow::Options{
                          .layer = ui::LayerWindow::Layer::Overlay,
                          .anchor = ui::LayerWindow::kTop | ui::LayerWindow::kBottom |
                                    ui::LayerWindow::kLeft | ui::LayerWindow::kRight,
                          .width = 0,
                          .height = 0,
                          .exclusive_zone = -1, // whole output: a click outside closes
                          .keyboard = ui::LayerWindow::Keyboard::Exclusive,
                          .name_space = "tetra-volume",
                          .output = nullptr,
                      });
    window_->on_draw = [this] { draw(); };
    window_->on_closed = [this] { shell_.hide_volume_popup(); };
}

void VolumePopup::draw() {
    const auto& p = shell_.app().palette();
    const Audio::State& audio = shell_.audio().state();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 pos{viewport->Size.x - kPanelWidth - kMargin,
                     viewport->Size.y - Bar::kHeight - kMargin - kPanelHeight};

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize({kPanelWidth, kPanelHeight});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16, 14});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::to_imvec4(p.surface));
    ImGui::Begin("##volume", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

    ImGui::TextUnformatted("Volume");
    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.text_dim));
    ImGui::TextUnformatted(audio.available ? audio.device.c_str() : "No sound server");
    ImGui::PopStyleColor();
    ImGui::Dummy({0, 4});

    // Mute toggle: the speaker icon, crossed out when muted.
    const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
    const char* icon_name = audio.muted ? "audio-volume-muted-symbolic" : "audio-volume-high-symbolic";
    const ImVec2 button_pos = ImGui::GetCursorScreenPos();
    const float button = ImGui::GetFrameHeight();
    ImGui::BeginDisabled(!audio.available);
    if (ImGui::Button("##mute", {button, button})) {
        shell_.audio().toggle_mute();
    }
    if (const ui::Icon* icon = shell_.icons().get(icon_name, static_cast<int>(kIconSize * scale))) {
        const float pad = std::round((button - kIconSize) / 2);
        ImGui::GetWindowDrawList()->AddImage(
            icon->ref(), {button_pos.x + pad, button_pos.y + pad},
            {button_pos.x + pad + kIconSize, button_pos.y + pad + kIconSize}, {0, 0}, {1, 1},
            ui::to_imu32(p.text));
    }

    ImGui::SameLine();
    float percent = std::round(audio.volume * 100);
    ImGui::SetNextItemWidth(-1);
    if (audio.muted) { // like the OSD: the level stays, greyed out
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ui::to_imvec4(p.text_dim));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ui::to_imvec4(p.text_dim));
    }
    if (ImGui::SliderFloat("##level", &percent, 0, 100, "%.0f%%")) {
        shell_.audio().set_volume(percent / 100);
    }
    if (audio.muted) {
        ImGui::PopStyleColor(2);
    }
    ImGui::EndDisabled();

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool inside = mouse.x >= pos.x && mouse.x < pos.x + kPanelWidth && mouse.y >= pos.y &&
                        mouse.y < pos.y + kPanelHeight;
    if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !inside) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        shell_.hide_volume_popup();
    }
}

} // namespace tetra::shell
