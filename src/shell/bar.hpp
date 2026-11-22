#pragma once

#include "app.hpp"
#include "window.hpp"

#include <memory>

namespace tetra::shell {

// The bottom bar on one output: launcher button in the centre, clock and
// (later) tray on the right. Task slots arrive in phase 2.
class Bar {
public:
    static constexpr int kHeight = 48;

    Bar(ui::App& app, const ui::OutputInfo& output);

    void redraw() { window_->request_redraw(); }

private:
    void draw();
    void draw_launcher(float x, float y, float size);
    void draw_clock(float right, float height);

    ui::App& app_;
    std::unique_ptr<ui::LayerWindow> window_;
};

} // namespace tetra::shell
