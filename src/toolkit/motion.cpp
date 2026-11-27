#include "motion.hpp"

#include "app.hpp"
#include "window.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace tetra::ui {

namespace {

float slow_factor() {
    static const float factor = [] {
        const char* value = std::getenv("TETRA_SLOW_ANIMATIONS"); // NOLINT(concurrency-mt-unsafe)
        const float f = value != nullptr ? std::strtof(value, nullptr) : 1.0F;
        return f >= 1 ? f : 1.0F;
    }();
    return factor;
}

} // namespace

float transition(const App& app, Window& window, std::chrono::steady_clock::time_point start,
                 std::chrono::milliseconds duration) {
    if (!app.config().theme.animations || duration.count() <= 0) {
        return 1;
    }
    const auto elapsed = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    const float t = std::clamp(elapsed / (static_cast<float>(duration.count()) * slow_factor()), 0.0F, 1.0F);
    if (t < 1) {
        window.request_redraw(2);
    }
    const float u = 1 - t;
    return 1 - (u * u * u);
}

void fade_since(ImDrawList* list, int first, float alpha, ImVec2 offset) {
    if (alpha >= 1 && offset.x == 0 && offset.y == 0) {
        return;
    }
    const auto factor = static_cast<float>(std::clamp(alpha, 0.0F, 1.0F));
    for (int i = std::max(first, 0); i < list->VtxBuffer.Size; ++i) {
        ImDrawVert& v = list->VtxBuffer[i];
        v.pos.x += offset.x;
        v.pos.y += offset.y;
        const auto a = static_cast<float>((v.col >> IM_COL32_A_SHIFT) & 0xFF);
        const auto faded = static_cast<ImU32>(std::lround(a * factor));
        v.col = (v.col & ~IM_COL32_A_MASK) | (faded << IM_COL32_A_SHIFT);
    }
}

} // namespace tetra::ui
