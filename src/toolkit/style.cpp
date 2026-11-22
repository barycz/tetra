#include "style.hpp"

namespace tetra::ui {

ImVec4 to_imvec4(const config::Color& c) {
    return {c.r, c.g, c.b, c.a};
}

ImU32 to_imu32(const config::Color& c) {
    return ImGui::ColorConvertFloat4ToU32(to_imvec4(c));
}

config::Color mix(const config::Color& base, const config::Color& overlay) {
    const float a = overlay.a;
    return {base.r + ((overlay.r - base.r) * a), base.g + ((overlay.g - base.g) * a),
            base.b + ((overlay.b - base.b) * a), base.a};
}

void apply_style(ImGuiStyle& style, const theme::Palette& p) {
    const float font_size_base = style.FontSizeBase;
    style = ImGuiStyle();
    style.FontSizeBase = font_size_base;

    style.WindowPadding = {12, 12};
    style.FramePadding = {10, 6};
    style.ItemSpacing = {8, 6};
    style.ItemInnerSpacing = {6, 4};
    style.WindowRounding = 8;
    style.ChildRounding = 8;
    style.FrameRounding = 6;
    style.PopupRounding = 8;
    style.ScrollbarRounding = 6;
    style.GrabRounding = 6;
    style.TabRounding = 6;
    style.WindowBorderSize = 0;
    style.FrameBorderSize = 0;
    style.PopupBorderSize = 1;
    style.ScrollbarSize = 12;

    const config::Color frame = mix(p.window, p.hover);
    const config::Color frame_hover = mix(frame, p.hover);
    const config::Color frame_active = mix(frame_hover, p.pressed);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = to_imvec4(p.text);
    c[ImGuiCol_TextDisabled] = to_imvec4(p.text_dim);
    c[ImGuiCol_WindowBg] = to_imvec4(p.window);
    c[ImGuiCol_ChildBg] = {0, 0, 0, 0};
    c[ImGuiCol_PopupBg] = to_imvec4(p.surface);
    c[ImGuiCol_Border] = to_imvec4(p.separator);
    c[ImGuiCol_BorderShadow] = {0, 0, 0, 0};
    c[ImGuiCol_FrameBg] = to_imvec4(frame);
    c[ImGuiCol_FrameBgHovered] = to_imvec4(frame_hover);
    c[ImGuiCol_FrameBgActive] = to_imvec4(frame_active);
    c[ImGuiCol_TitleBg] = to_imvec4(p.titlebar_inactive);
    c[ImGuiCol_TitleBgActive] = to_imvec4(p.titlebar_active);
    c[ImGuiCol_TitleBgCollapsed] = to_imvec4(p.titlebar_inactive);
    c[ImGuiCol_MenuBarBg] = to_imvec4(p.surface);
    c[ImGuiCol_ScrollbarBg] = {0, 0, 0, 0};
    c[ImGuiCol_ScrollbarGrab] = to_imvec4(frame_hover);
    c[ImGuiCol_ScrollbarGrabHovered] = to_imvec4(mix(frame_hover, p.hover));
    c[ImGuiCol_ScrollbarGrabActive] = to_imvec4(p.accent);
    c[ImGuiCol_CheckMark] = to_imvec4(p.accent);
    c[ImGuiCol_SliderGrab] = to_imvec4(p.accent);
    c[ImGuiCol_SliderGrabActive] = to_imvec4(p.accent);
    c[ImGuiCol_Button] = to_imvec4(frame);
    c[ImGuiCol_ButtonHovered] = to_imvec4(frame_hover);
    c[ImGuiCol_ButtonActive] = to_imvec4(frame_active);
    c[ImGuiCol_Header] = to_imvec4(frame);
    c[ImGuiCol_HeaderHovered] = to_imvec4(frame_hover);
    c[ImGuiCol_HeaderActive] = to_imvec4(frame_active);
    c[ImGuiCol_Separator] = to_imvec4(p.separator);
    c[ImGuiCol_SeparatorHovered] = to_imvec4(p.accent);
    c[ImGuiCol_SeparatorActive] = to_imvec4(p.accent);
    c[ImGuiCol_ResizeGrip] = {0, 0, 0, 0};
    c[ImGuiCol_ResizeGripHovered] = to_imvec4(p.accent.with_alpha(0.6F));
    c[ImGuiCol_ResizeGripActive] = to_imvec4(p.accent);
    c[ImGuiCol_Tab] = to_imvec4(frame);
    c[ImGuiCol_TabHovered] = to_imvec4(frame_hover);
    c[ImGuiCol_TabSelected] = to_imvec4(frame_active);
    c[ImGuiCol_TextSelectedBg] = to_imvec4(p.accent.with_alpha(0.35F));
    c[ImGuiCol_NavCursor] = to_imvec4(p.accent);
    c[ImGuiCol_ModalWindowDimBg] = {0, 0, 0, 0.45F};
}

} // namespace tetra::ui
