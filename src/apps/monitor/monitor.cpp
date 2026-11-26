#include "monitor.hpp"

#include "style.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <format>

namespace tetra::monitor {

namespace {

constexpr float kSidebarWidth = 208;
constexpr float kIconSize = 18;
constexpr int kMessageMs = 6000;

std::string utf8(uint32_t c) {
    std::string out;
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    return out;
}

std::string interval_label(int ms) {
    if (ms == 0) {
        return "Paused";
    }
    return ms % 1000 == 0 ? std::format("{} s", ms / 1000) : std::format("{:.1f} s", ms / 1000.0);
}

} // namespace

std::string format_bytes(double bytes) {
    static constexpr std::array kUnits = {"B", "kB", "MB", "GB", "TB", "PB"};
    std::size_t unit = 0;
    while (bytes >= 1000 && unit + 1 < kUnits.size()) {
        bytes /= 1000;
        ++unit;
    }
    if (unit == 0) {
        return std::format("{:.0f} B", bytes);
    }
    return bytes < 99.95 ? std::format("{:.1f} {}", bytes, kUnits.at(unit))
                         : std::format("{:.0f} {}", bytes, kUnits.at(unit));
}

std::string format_rate(double bytes_per_second) {
    return format_bytes(bytes_per_second) + "/s";
}

const std::vector<Monitor::PageInfo>& Monitor::pages() {
    static const std::vector<PageInfo> list = {
        {Page::Processes, "processes", "Processes", "view-list-symbolic"},
        {Page::Resources, "resources", "Resources", "power-profile-performance-symbolic"},
        {Page::FileSystems, "filesystems", "File systems", "drive-harddisk-symbolic"},
    };
    return list;
}

Monitor::Monitor(ui::App& app) : app_(app), icons_(app) {
    sampler_.sample(0);
    last_sample_ = std::chrono::steady_clock::now();
    window_ = std::make_unique<ui::ToplevelWindow>(
        app_, ui::ToplevelWindow::Options{.title = "System Monitor", .width = 960, .height = 600});
    window_->on_draw = [this] { draw(); };
    window_->on_key = [this](const ui::KeyEvent& key) {
        // Typing anywhere on the Processes page searches.
        if (page_ != Page::Processes || confirm_ || key.ctrl || key.alt || key.super) {
            if (page_ == Page::Processes && key.ctrl && !key.alt && (key.keysym == 'f' || key.keysym == 'F')) {
                focus_search_ = true;
                window_->request_redraw();
                return true;
            }
            return false;
        }
        if (!search_active_ && key.codepoint >= 0x20 && key.codepoint != 0x7F) {
            typed_ += utf8(key.codepoint);
            focus_search_ = true;
            window_->request_redraw();
            return true;
        }
        return false;
    };
    app_.on_config_changed = [this] { window_->request_redraw(); };
    schedule();
}

void Monitor::show_page(std::string_view id) {
    for (const auto& info : pages()) {
        if (id == info.id) {
            page_ = info.page;
        }
    }
    window_->request_redraw();
}

void Monitor::set_search(std::string_view text) {
    std::snprintf(search_.data(), search_.size(), "%.*s", static_cast<int>(text.size()), text.data());
    window_->request_redraw();
}

void Monitor::schedule() {
    if (timer_) {
        app_.cancel_timer(*timer_);
        timer_.reset();
    }
    if (interval_ > 0) {
        timer_ = app_.add_timer(std::chrono::milliseconds(interval_), [this] {
            timer_.reset();
            tick();
            schedule();
        });
    }
}

void Monitor::tick() {
    const auto now = std::chrono::steady_clock::now();
    sampler_.sample(std::chrono::duration<double>(now - last_sample_).count());
    last_sample_ = now;
    if (selected_ && selected_row() == nullptr) {
        selected_.reset(); // it exited
    }
    window_->request_redraw();
}

void Monitor::show_message(std::string text) {
    message_ = std::move(text);
    if (message_timer_) {
        app_.cancel_timer(*message_timer_);
    }
    message_timer_ = app_.add_timer(std::chrono::milliseconds(kMessageMs), [this] {
        message_timer_.reset();
        message_.clear();
        window_->request_redraw();
    });
    window_->request_redraw();
}

void Monitor::draw() {
    const auto& p = app_.palette();
    // Up/Down move the process selection, not ImGui's keyboard navigation.
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::to_imvec4(p.window));
    ImGui::Begin("##monitor", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(3);

    draw_sidebar(kSidebarWidth, viewport->Size.y);

    constexpr float kPadding = 20;
    ImGui::SetCursorPos({kSidebarWidth, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {kPadding, kPadding - 4});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::to_imvec4(p.window));
    const ImGuiWindowFlags scroll =
        page_ == Page::Processes ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : 0;
    ImGui::BeginChild("##page", {viewport->Size.x - kSidebarWidth, viewport->Size.y},
                      ImGuiChildFlags_AlwaysUseWindowPadding, scroll);
    switch (page_) {
    case Page::Processes:
        page_processes();
        break;
    case Page::Resources:
        page_resources();
        break;
    case Page::FileSystems:
        page_filesystems();
        break;
    }
    if (page_ != Page::Processes) {
        ImGui::Dummy({0, kPadding});
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    draw_message();

    ImGui::End();
    ImGui::PopStyleColor();
}

void Monitor::draw_sidebar(float width, float height) {
    const auto& p = app_.palette();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled({0, 0}, {width, height}, ui::to_imu32(p.surface));
    dl->AddLine({width - 0.5F, 0}, {width - 0.5F, height}, ui::to_imu32(p.separator));

    ImGui::SetCursorPos({20, 22});
    ImGui::PushFont(nullptr, 17);
    ImGui::TextUnformatted("System Monitor");
    ImGui::PopFont();

    const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
    float y = 64;
    for (const auto& info : pages()) {
        const bool selected = info.page == page_;
        ImGui::SetCursorPos({10, y});
        ImGui::PushID(info.id);
        if (ImGui::InvisibleButton("##page", {width - 20, 38})) {
            page_ = info.page;
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        if (selected || hovered) {
            dl->AddRectFilled({10, y}, {width - 10, y + 38}, ui::to_imu32(selected ? p.pressed : p.hover), 8);
        }
        if (selected) {
            dl->AddRectFilled({10, y + 10}, {13, y + 28}, ui::to_imu32(p.accent), 2);
        }
        if (const ui::Icon* icon = icons_.get(info.icon, static_cast<int>(kIconSize * scale))) {
            dl->AddImage(icon->ref(), {24, y + 10}, {24 + kIconSize, y + 10 + kIconSize}, {0, 0}, {1, 1},
                         ui::to_imu32(selected ? p.text : p.text_dim));
        }
        dl->AddText({54, y + 10}, ui::to_imu32(p.text), info.title);
        y += 42;
    }

    // At a glance: CPU and memory, and how often to update.
    const float graph_width = width - 40;
    float bottom = height - 20;
    ImGui::PushFont(nullptr, 13);
    const float line = ImGui::GetTextLineHeight();
    const float combo_y = bottom - ImGui::GetFrameHeight();
    ImGui::SetCursorPos({20, combo_y - line - 4});
    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.text_dim));
    ImGui::TextUnformatted("Update every");
    ImGui::PopStyleColor();
    ImGui::SetCursorPos({20, combo_y});
    ImGui::SetNextItemWidth(graph_width);
    if (ImGui::BeginCombo("##interval", interval_label(interval_).c_str())) {
        for (int ms : kIntervals) {
            if (ImGui::Selectable(interval_label(ms).c_str(), ms == interval_)) {
                interval_ = ms;
                schedule();
            }
        }
        if (ImGui::Selectable("Paused", interval_ == 0)) {
            interval_ = 0;
            schedule();
        }
        ImGui::EndCombo();
    }
    bottom = combo_y - line - 18;

    const auto glance = [&](const char* label, const History& history, const std::string& value) {
        constexpr float kGraph = 34;
        const float top = bottom - kGraph - line - 4;
        dl->AddText({20, top}, ui::to_imu32(p.text_dim), label);
        const float value_width = ImGui::CalcTextSize(value.c_str()).x;
        dl->AddText({20 + graph_width - value_width, top}, ui::to_imu32(p.text), value.c_str());
        ImGui::SetCursorPos({20, top + line + 4});
        graph(label, {graph_width, kGraph}, {{&history, ui::to_imu32(p.accent)}}, 100, nullptr);
        bottom = top - 14;
    };
    if (sampler_.memory()) {
        glance("Memory", sampler_.memory_history(), std::format("{:.0f}%", sampler_.memory_history().last()));
    }
    glance("CPU", sampler_.cpu(), std::format("{:.0f}%", sampler_.cpu().last()));
    ImGui::PopFont();
}

void Monitor::draw_message() {
    if (message_.empty()) {
        return;
    }
    const auto& p = app_.palette();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 text = ImGui::CalcTextSize(message_.c_str());
    const float page_width = viewport->Size.x - kSidebarWidth;
    const ImVec2 size{std::min(text.x + 32, page_width - 40), text.y + 20};
    const ImVec2 pos{kSidebarWidth + ((page_width - size.x) / 2), viewport->Size.y - size.y - 52};
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(pos, {pos.x + size.x, pos.y + size.y}, ui::to_imu32(p.surface), 8);
    dl->AddRect(pos, {pos.x + size.x, pos.y + size.y}, ui::to_imu32(p.close_hover), 8, 1.5F);
    dl->PushClipRect(pos, {pos.x + size.x - 16, pos.y + size.y}, true);
    dl->AddText({pos.x + 16, pos.y + 10}, ui::to_imu32(p.text), message_.c_str());
    dl->PopClipRect();
}

} // namespace tetra::monitor
