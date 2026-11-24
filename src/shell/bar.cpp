#include "bar.hpp"

#include "shell.hpp"
#include "sni.hpp"
#include "style.hpp"
#include "texture.hpp"

#include <imgui.h>

#include <array>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <format>
#include <string>

namespace tetra::shell {

namespace {

constexpr float kLauncherSize = 40;
constexpr float kLauncherGap = 10;
constexpr float kSlotWidth = 44;
constexpr float kSlotHeight = 40;
constexpr float kGroupGap = 14;
constexpr float kIconSize = 24;
constexpr float kTrayIconSize = 16;

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

Bar::Bar(Shell& shell, const ui::OutputInfo& output)
    : shell_(shell), output_(output.output),
      output_height_(static_cast<float>(output.height) / static_cast<float>(std::max(output.scale, 1))) {
    window_ = std::make_unique<ui::LayerWindow>(
        shell_.app(), ui::LayerWindow::Options{
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
    const auto& p = shell_.app().palette();
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

    // Centre: launcher, then three groups of four slots, like the F-row.
    const float total = kLauncherSize + kLauncherGap + (Shell::kSlots * kSlotWidth) + (2 * kGroupGap);
    float x = std::round((width - total) / 2);
    draw_launcher(x, std::round((height - kLauncherSize) / 2), kLauncherSize);
    x += kLauncherSize + kLauncherGap;
    const float slot_y = std::round((height - kSlotHeight) / 2);
    for (std::size_t i = 0; i < Shell::kSlots; ++i) {
        if (i > 0 && i % 4 == 0) {
            x += kGroupGap;
        }
        draw_slot(i, shell_.slots().at(i), x, slot_y);
        x += kSlotWidth;
    }

    // Right: clock, tray to its left.
    constexpr float kClockWidth = 76;
    draw_clock(width - 14, height);
    draw_tray(width - 14 - kClockWidth, height);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void Bar::draw_launcher(float x, float y, float size) {
    const auto& p = shell_.app().palette();
    ImGui::SetCursorPos({x, y});
    if (ImGui::InvisibleButton("##launcher", {size, size})) {
        shell_.toggle_launcher();
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered()) {
        dl->AddRectFilled({x, y}, {x + size, y + size},
                          ui::to_imu32(ImGui::IsItemActive() ? p.pressed : p.hover), 8);
    }
    draw_logo(dl, {x + (size / 2), y + (size / 2) + 1}, size * 0.55F, ui::to_imu32(p.accent));
}

void Bar::draw_slot(std::size_t index, const Slot& slot, float x, float y) {
    const auto& p = shell_.app().palette();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 min{x + 2, y};
    const ImVec2 max{x + kSlotWidth - 2, y + kSlotHeight};
    const bool used = !slot.app_id.empty();

    ImGui::PushID(static_cast<int>(index));
    ImGui::SetCursorPos({x, y});
    if (ImGui::InvisibleButton("##slot", {kSlotWidth, kSlotHeight}) && used) {
        shell_.activate_slot(index);
    }
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && used && ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
        shell_.launch_slot(index); // middle click: new instance
    }
    ImGui::PopID();

    // Background: focused window's slot is highlighted, hover on top.
    if (slot.focused) {
        dl->AddRectFilled(min, max, ui::to_imu32(p.hover), 6);
    }
    if (hovered && used) {
        dl->AddRectFilled(min, max, ui::to_imu32(ImGui::IsItemActive() ? p.pressed : p.hover), 6);
    }
    if (!used) {
        // Free slot: a faint outline keeps the F-key grid visible.
        dl->AddRect({min.x + 3, min.y + 3}, {max.x - 3, max.y - 3}, ui::to_imu32(p.separator), 5);
    }

    // F-key number, small and quiet, top-left.
    const std::string label = std::format("F{}", index + 1);
    ImGui::PushFont(nullptr, 9);
    dl->AddText({min.x + 3, min.y + 1}, ui::to_imu32(p.text_dim.with_alpha(used ? 0.75F : 0.5F)),
                label.c_str());
    ImGui::PopFont();

    if (used) {
        const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
        const int pixels = static_cast<int>(kIconSize * scale);
        const ui::Icon* icon = shell_.icons().get(slot.icon, pixels);
        if (icon == nullptr) {
            icon = shell_.icons().get("application-x-executable", pixels);
        }
        const ImVec2 icon_min{std::round(x + ((kSlotWidth - kIconSize) / 2)), y + 6};
        const ImVec2 icon_max{icon_min.x + kIconSize, icon_min.y + kIconSize};
        const float alpha = slot.windows == 0 || slot.minimized ? 0.75F : 1.0F;
        if (icon != nullptr) {
            const ImU32 tint = icon->symbolic ? ui::to_imu32(p.text.with_alpha(alpha))
                                              : ui::to_imu32(config::Color{1, 1, 1, alpha});
            dl->AddImage(icon->ref(), icon_min, icon_max, {0, 0}, {1, 1}, tint);
        }

        // Running indicator: short line; long accent line when focused.
        if (slot.windows > 0) {
            const float line = slot.focused ? 16 : 6;
            const float cx = x + (kSlotWidth / 2);
            const config::Color& color = slot.focused ? p.accent : p.text_dim;
            dl->AddRectFilled({cx - (line / 2), max.y - 4}, {cx + (line / 2), max.y - 2},
                              ui::to_imu32(color), 1);
        }
        // Stacked windows: count badge.
        if (slot.windows > 1) {
            const ImVec2 center{icon_max.x + 1, icon_min.y + 2};
            dl->AddCircleFilled(center, 7, ui::to_imu32(p.accent));
            const std::string count = slot.windows > 9 ? "9+" : std::to_string(slot.windows);
            ImGui::PushFont(nullptr, 10);
            const ImVec2 size = ImGui::CalcTextSize(count.c_str());
            dl->AddText({std::round(center.x - (size.x / 2)), std::round(center.y - (size.y / 2))},
                        ui::to_imu32(p.accent_text), count.c_str());
            ImGui::PopFont();
        }
    }
}

float Bar::draw_tray(float right, float height) {
    const auto& p = shell_.app().palette();
    const auto& state = shell_.tray().state();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
    const int pixels = static_cast<int>(kTrayIconSize * scale);
    const float y = std::round((height - kTrayIconSize) / 2);

    float x = right;
    auto draw_icon = [&](const std::string& name) {
        const ui::Icon* icon = shell_.icons().get(name, pixels);
        if (icon == nullptr) {
            return;
        }
        x -= kTrayIconSize;
        const ImU32 tint = icon->symbolic ? ui::to_imu32(p.text) : IM_COL32_WHITE;
        dl->AddImage(icon->ref(), {x, y}, {x + kTrayIconSize, y + kTrayIconSize}, {0, 0}, {1, 1}, tint);
        x -= 12;
    };
    if (!state.battery_icon.empty()) {
        draw_icon(state.battery_icon);
    }

    // Volume: click opens the popup, the wheel steps the level.
    {
        constexpr float kPad = 6;
        const float button_w = kTrayIconSize + (2 * kPad);
        const float button_h = height - 12;
        const float bx = x - kTrayIconSize - kPad;
        const float by = std::round((height - button_h) / 2);
        ImGui::SetCursorPos({bx, by});
        if (ImGui::InvisibleButton("##volume", {button_w, button_h})) {
            shell_.toggle_volume_popup();
        }
        if (ImGui::IsItemHovered()) {
            dl->AddRectFilled({bx, by}, {bx + button_w, by + button_h},
                              ui::to_imu32(ImGui::IsItemActive() ? p.pressed : p.hover), 6);
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0) {
                shell_.step_volume(wheel > 0 ? 0.05F : -0.05F);
            }
        }
        draw_icon(Shell::volume_icon(shell_.audio().state()));
    }

    draw_icon(state.network_icon);
    draw_sni_items(x, height);
    return x;
}

void Bar::draw_sni_items(float& x, float height) {
    SniTray* sni = shell_.sni();
    if (sni == nullptr) {
        return;
    }
    const auto& p = shell_.app().palette();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
    const int pixels = static_cast<int>(kTrayIconSize * scale);
    constexpr float kPad = 6;
    const float button_w = kTrayIconSize + (2 * kPad);
    const float button_h = height - 12;
    const float by = std::round((height - button_h) / 2);

    x -= 4; // a little more room between our icons and the apps'
    for (const auto& owned : sni->items()) {
        SniItem& item = *owned;
        if (!item.visible()) {
            continue;
        }
        const float bx = x - kTrayIconSize - kPad;
        ImGui::PushID(&item);
        ImGui::SetCursorPos({bx, by});
        ImGui::InvisibleButton("##sni", {button_w, button_h},
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                   ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered = ImGui::IsItemHovered();
        // Where the icon is, in output coordinates (apps position their
        // own windows from it).
        const float anchor = bx + (button_w / 2);
        const int ax = static_cast<int>(anchor);
        const int ay = static_cast<int>(output_height_ - Bar::kHeight + (height / 2));
        SniItem* raw = &item;
        wl_output* output = output_;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            sni->activate(item, ax, ay, [this, raw, output, anchor] {
                shell_.open_tray_menu(*raw, output, anchor);
            });
        } else if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            if (!item.menu_path.empty()) {
                shell_.open_tray_menu(item, output, anchor);
            } else {
                sni->context_menu(item, ax, ay);
            }
        } else if (ImGui::IsItemClicked(ImGuiMouseButton_Middle)) {
            sni->secondary_activate(item, ax, ay);
        }
        if (hovered) {
            dl->AddRectFilled({bx, by}, {bx + button_w, by + button_h},
                              ui::to_imu32(ImGui::IsItemActive() ? p.pressed : p.hover), 6);
            if (const float wheel = ImGui::GetIO().MouseWheel; wheel != 0) {
                sni->scroll(item, wheel > 0 ? 120 : -120); // one notch, Qt units
            }
        }
        ImGui::PopID();

        // The icon: a themed name (the app's own theme dir first), else
        // the pixmap the app sent, else a generic one.
        const ImVec2 icon_min{x - kTrayIconSize, std::round((height - kTrayIconSize) / 2)};
        const ImVec2 icon_max{icon_min.x + kTrayIconSize, icon_min.y + kTrayIconSize};
        const std::string& name = item.needs_attention() && !item.attention_icon_name.empty()
                                      ? item.attention_icon_name
                                      : item.icon_name;
        const ui::Icon* icon = nullptr;
        if (!name.empty() && !item.icon_theme_path.empty()) {
            for (const char* ext : {".png", ".svg"}) {
                const std::filesystem::path file =
                    std::filesystem::path(item.icon_theme_path) / (name + ext);
                std::error_code ec;
                if (std::filesystem::exists(file, ec)) {
                    icon = shell_.icons().get(file.string(), pixels);
                    break;
                }
            }
        }
        if (icon == nullptr && !name.empty()) {
            icon = shell_.icons().get(name, pixels);
        }
        if (icon == nullptr) {
            if (const SniItem::Pixmap* pixmap = item.best_pixmap(pixels)) {
                if (item.texture_dirty || !item.texture) {
                    // ARGB, big-endian -> RGBA.
                    std::vector<uint8_t> rgba(pixmap->argb.size());
                    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
                        rgba[i] = pixmap->argb[i + 1];
                        rgba[i + 1] = pixmap->argb[i + 2];
                        rgba[i + 2] = pixmap->argb[i + 3];
                        rgba[i + 3] = pixmap->argb[i];
                    }
                    item.texture = std::make_unique<ui::Texture>(pixmap->width, pixmap->height,
                                                                 pixmap->width * 4, 4, rgba);
                    item.texture_dirty = false;
                }
                dl->AddImage(item.texture->ref(), icon_min, icon_max);
            } else {
                icon = shell_.icons().get("application-x-executable", pixels);
            }
        }
        if (icon != nullptr) {
            const ImU32 tint = icon->symbolic ? ui::to_imu32(p.text) : IM_COL32_WHITE;
            dl->AddImage(icon->ref(), icon_min, icon_max, {0, 0}, {1, 1}, tint);
        }
        x -= kTrayIconSize + 12;
    }
}

void Bar::draw_clock(float right, float height) {
    const auto& p = shell_.app().palette();
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
    dl->AddText({std::round(right - date_size.x), top + time_size.y + gap},
                ui::to_imu32(p.text_dim), date.c_str());
    ImGui::PopFont();
}

} // namespace tetra::shell
