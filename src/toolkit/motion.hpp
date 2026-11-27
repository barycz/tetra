#pragma once

#include <imgui.h>

#include <chrono>

namespace tetra::ui {

class App;
class Window;

// Animations in the shell and apps (theme.animations). Windows render on
// demand only, so a running transition keeps its window drawing.

// Eased (ease-out) progress, 0 to 1, of a transition that began at `start`;
// 1 at once when animations are off. While below 1, `window` keeps
// rendering. TETRA_SLOW_ANIMATIONS=N makes every transition N times slower.
float transition(const App& app, Window& window, std::chrono::steady_clock::time_point start,
                 std::chrono::milliseconds duration);

// Fades (multiplies the alpha of) and moves everything drawn into `list`
// since vertex `first` (its VtxBuffer.Size before drawing): one card of
// several. Hit-testing stays where the items were laid out.
void fade_since(ImDrawList* list, int first, float alpha, ImVec2 offset = {});

} // namespace tetra::ui
