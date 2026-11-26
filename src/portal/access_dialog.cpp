#include "access_dialog.hpp"

#include "style.hpp"

#include <imgui.h>

#include <algorithm>

namespace tetra::portal {

AccessDialog::AccessDialog(ui::App& app, AccessOptions options, std::function<void(bool, std::vector<Choice>)> done)
    : app_(app), icons_(app), options_(std::move(options)), done_(std::move(done)) {
    std::erase(options_.grant_label, '_'); // GTK mnemonics
    std::erase(options_.deny_label, '_');
    if (options_.grant_label.empty()) {
        options_.grant_label = "Allow";
    }
    if (options_.deny_label.empty()) {
        options_.deny_label = "Deny";
    }
    const int height = 150 + (options_.body.empty() ? 0 : 30) + (static_cast<int>(options_.choices.size()) * 34);
    window_ = std::make_unique<ui::ToplevelWindow>(
        app_, ui::ToplevelWindow::Options{.title = options_.title.empty() ? "Allow access?" : options_.title,
                                          .width = 480,
                                          .height = height,
                                          .parent = options_.parent});
    window_->on_draw = [this] { draw(); };
    window_->on_close = [this] { finish(false); };
}

AccessDialog::~AccessDialog() = default;

void AccessDialog::cancel() {
    finished_ = true;
    done_ = nullptr;
}

void AccessDialog::finish(bool granted) {
    if (finished_) {
        return;
    }
    finished_ = true;
    if (done_) {
        auto done = std::move(done_);
        done_ = nullptr;
        done(granted, options_.choices);
    }
}

void AccessDialog::draw() {
    if (finished_) {
        return;
    }
    const auto& p = app_.palette();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {24, 20});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::to_imvec4(p.window));
    ImGui::Begin("##access", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(3);

    constexpr float kIcon = 40;
    const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
    const ui::Icon* icon =
        icons_.get(options_.icon.empty() ? std::string("dialog-question-symbolic") : options_.icon,
                   static_cast<int>(kIcon * scale));
    if (icon != nullptr) {
        ImGui::Image(icon->ref(), {kIcon, kIcon}, {0, 0}, {1, 1});
        ImGui::SameLine(0, 16);
    }
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::PushFont(nullptr, 17);
    ImGui::TextWrapped("%s", options_.title.c_str());
    ImGui::PopFont();
    if (!options_.subtitle.empty()) {
        ImGui::Dummy({0, 2});
        ImGui::TextWrapped("%s", options_.subtitle.c_str());
    }
    if (!options_.body.empty()) {
        ImGui::Dummy({0, 2});
        ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.text_dim));
        ImGui::TextWrapped("%s", options_.body.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::PopTextWrapPos();
    for (auto& choice : options_.choices) {
        ImGui::PushID(choice.id.c_str());
        ImGui::Dummy({0, 4});
        if (choice.options.empty()) {
            bool on = choice.selected == "true";
            if (ImGui::Checkbox(choice.label.c_str(), &on)) {
                choice.selected = on ? "true" : "false";
            }
        } else {
            std::string current = choice.selected;
            for (const auto& [id, label] : choice.options) {
                if (id == choice.selected) {
                    current = label;
                }
            }
            ImGui::SetNextItemWidth(200);
            if (ImGui::BeginCombo(choice.label.c_str(), current.c_str())) {
                for (const auto& [id, label] : choice.options) {
                    if (ImGui::Selectable(label.c_str(), id == choice.selected)) {
                        choice.selected = id;
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::PopID();
    }
    ImGui::EndGroup();

    // The buttons at the bottom right; Enter allows, Escape denies.
    constexpr float kButton = 120;
    const float y = ImGui::GetWindowHeight() - ImGui::GetFrameHeight() - 20;
    ImGui::SetCursorPos({ImGui::GetWindowWidth() - 24 - (2 * kButton) - 8, std::max(ImGui::GetCursorPosY() + 12, y)});
    if (ImGui::Button(options_.deny_label.c_str(), {kButton, 0}) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        finish(false);
    }
    ImGui::SameLine(0, 8);
    ImGui::PushStyleColor(ImGuiCol_Button, ui::to_imvec4(p.accent));
    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.accent_text));
    if (ImGui::Button(options_.grant_label.c_str(), {kButton, 0}) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
        finish(true);
    }
    ImGui::PopStyleColor(2);

    ImGui::End();
    ImGui::PopStyleColor();
}

} // namespace tetra::portal
