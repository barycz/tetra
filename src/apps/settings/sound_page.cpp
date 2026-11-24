// Settings: sound. Unlike the other pages this is not config.json: the
// default devices and their volume live in the sound server (PipeWire),
// which remembers them itself.

#include "settings.hpp"

#include "style.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace tetra::settings {

namespace {

constexpr float kMuteIconSize = 18;

} // namespace

bool Settings::mute_button(const char* id, bool muted, const char* icon_on, const char* icon_off) {
    const auto& p = app_.palette();
    const float size = ImGui::GetFrameHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::Button(id, {size, size});
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", muted ? "Unmute" : "Mute");
    }
    const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
    if (const ui::Icon* icon = icons_.get(muted ? icon_off : icon_on, static_cast<int>(kMuteIconSize * scale))) {
        const float pad = std::round((size - kMuteIconSize) / 2);
        ImGui::GetWindowDrawList()->AddImage(icon->ref(), {pos.x + pad, pos.y + pad},
                                             {pos.x + pad + kMuteIconSize, pos.y + pad + kMuteIconSize}, {0, 0},
                                             {1, 1}, ui::to_imu32(muted ? p.text_dim : p.text));
    }
    return clicked;
}

// A device list: the default device, or another to make the default.
std::optional<std::string> Settings::device_combo(const char* id, const std::vector<Audio::Device>& devices,
                                                  const std::string& current, const char* none) {
    std::optional<std::string> chosen;
    const auto it = std::ranges::find(devices, current, &Audio::Device::name);
    const std::string preview = it != devices.end() ? it->description : none;
    ImGui::BeginDisabled(devices.empty());
    if (ImGui::BeginCombo(id, preview.c_str())) {
        for (const auto& d : devices) {
            if (ImGui::Selectable(std::format("{}##{}", d.description, d.name).c_str(), d.name == current) &&
                d.name != current) {
                chosen = d.name;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    return chosen;
}

void Settings::page_sound() {
    const Audio::State& s = audio_.state();
    if (!s.available) {
        begin_card(nullptr);
        row("No sound server", "Sound settings appear once PipeWire (or PulseAudio) is running.");
        end_card();
        return;
    }
    const float spacing = ImGui::GetStyle().ItemSpacing.x;

    begin_card("Output");
    row("Output device", s.outputs.size() > 1 ? "Where sound plays. Apps that are playing move along." : nullptr);
    if (auto chosen = device_combo("##output", s.outputs, s.output, "No output")) {
        audio_.set_output(*chosen);
    }
    row("Volume");
    ImGui::BeginDisabled(s.output.empty());
    if (mute_button("##mute", s.muted, "audio-volume-high-symbolic", "audio-volume-muted-symbolic")) {
        audio_.set_muted(!s.muted);
    }
    ImGui::SameLine(0, spacing);
    {
        ImGui::SetNextItemWidth(control_width() - ImGui::GetFrameHeight() - spacing);
        float percent = std::round(s.volume * 100);
        if (ImGui::SliderFloat("##volume", &percent, 0, 100, "%.0f%%")) {
            audio_.set_volume(percent / 100);
        }
    }
    ImGui::EndDisabled();
    end_card();

    begin_card("Input");
    row("Input device", s.inputs.empty() ? "No microphone is connected." : nullptr);
    if (auto chosen = device_combo("##input", s.inputs, s.input, "No microphone")) {
        audio_.set_input(*chosen);
    }
    row("Input volume", "Microphone mute: the mic mute key does the same.");
    ImGui::BeginDisabled(s.input.empty());
    if (mute_button("##micmute", s.input_muted, "audio-input-microphone-symbolic",
                    "microphone-sensitivity-muted-symbolic")) {
        audio_.set_input_muted(!s.input_muted);
    }
    ImGui::SameLine(0, spacing);
    {
        ImGui::SetNextItemWidth(control_width() - ImGui::GetFrameHeight() - spacing);
        float percent = std::round(s.input_volume * 100);
        if (ImGui::SliderFloat("##inputvolume", &percent, 0, 100, "%.0f%%")) {
            audio_.set_input_volume(percent / 100);
        }
    }
    ImGui::EndDisabled();
    end_card();
}

} // namespace tetra::settings
