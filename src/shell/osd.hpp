#pragma once

#include "app.hpp"
#include "window.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace tetra::shell {

class Shell;

// On-screen display for volume and brightness changes: a small panel
// centred above the bar that hides itself after a moment.
class Osd {
public:
    explicit Osd(Shell& shell);
    ~Osd();

    Osd(const Osd&) = delete;
    Osd& operator=(const Osd&) = delete;
    Osd(Osd&&) = delete;
    Osd& operator=(Osd&&) = delete;

    // `level` 0..1; `dimmed` greys the bar out (muted).
    void show(std::string icon, float level, bool dimmed);

private:
    void draw();
    void hide();

    Shell& shell_;
    std::shared_ptr<ui::LayerWindow> window_;
    std::optional<ui::App::TimerId> hide_timer_;
    std::chrono::steady_clock::time_point appeared_;
    std::string icon_;
    float level_ = 0;
    bool dimmed_ = false;
};

} // namespace tetra::shell
