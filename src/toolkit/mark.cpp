#include "mark.hpp"

#include <array>

namespace tetra::ui {

namespace {

ImVec2 lerp(ImVec2 a, ImVec2 b, float t) {
    return {a.x + ((b.x - a.x) * t), a.y + ((b.y - a.y) * t)};
}

} // namespace

void draw_mark(ImDrawList* dl, ImVec2 center, float size, ImU32 color) {
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
        dl->AddTriangleFilled(lerp(tri[0], c, kGap), lerp(tri[1], c, kGap), lerp(tri[2], c, kGap), color);
    }
}

} // namespace tetra::ui
