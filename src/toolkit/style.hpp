#pragma once

#include "theme.hpp"

#include <imgui.h>

namespace tetra::ui {

[[nodiscard]] ImVec4 to_imvec4(const config::Color& color);
[[nodiscard]] ImU32 to_imu32(const config::Color& color);
// `overlay` (with its alpha) composited onto an opaque `base`.
[[nodiscard]] config::Color mix(const config::Color& base, const config::Color& overlay);

// tetra's ImGui look: rounding, spacing and colours from the palette.
void apply_style(ImGuiStyle& style, const theme::Palette& palette);

} // namespace tetra::ui
