#pragma once

#include "window.hpp"

#include <memory>

namespace tetra::shell {

class Shell;

// Volume control opened from the tray: output device, mute button and a
// slider, above the tray. Escape or a click outside closes it.
class VolumePopup {
public:
    explicit VolumePopup(Shell& shell);

    void redraw() { window_->request_redraw(); }

private:
    void draw();

    Shell& shell_;
    std::unique_ptr<ui::LayerWindow> window_;
};

} // namespace tetra::shell
