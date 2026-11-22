#pragma once

#include <cstdint>

namespace tetra::ui {

// xkb keysym (unshifted level) -> ImGuiKey; ImGuiKey_None if unmapped.
int keysym_to_imgui_key(uint32_t keysym);

} // namespace tetra::ui
