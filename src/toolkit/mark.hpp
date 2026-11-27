#pragma once

#include <imgui.h>

namespace tetra::ui {

// The tetra mark: a triangle split into four, the three corner triangles
// filled, a flattened tetrahedron (the launcher button, Settings). `size` is
// the triangle's side. The compositor draws the large desktop version with
// cairo (src/compositor/wallpaper.cpp), with the same geometry.
void draw_mark(ImDrawList* dl, ImVec2 center, float size, ImU32 color);

} // namespace tetra::ui
