#pragma once

#include "app.hpp"
#include "window.hpp"

#include <cstddef>
#include <memory>

namespace tetra::shell {

class Shell;
struct Slot;

// The bottom bar on one output. Centre: launcher button and the 12 task
// slots in three groups of four (F1-F4, F5-F8, F9-F12). Right: tray and clock.
class Bar {
public:
    static constexpr int kHeight = 48;

    Bar(Shell& shell, const ui::OutputInfo& output);

    void redraw() { window_->request_redraw(); }

private:
    void draw();
    void draw_launcher(float x, float y, float size);
    void draw_slot(std::size_t index, const Slot& slot, float x, float y);
    float draw_tray(float right, float height);
    void draw_clock(float right, float height);

    void draw_sni_items(float& x, float height);

    Shell& shell_;
    wl_output* output_;
    float output_height_; // logical, for coordinates sent to tray apps
    std::unique_ptr<ui::LayerWindow> window_;
};

} // namespace tetra::shell
