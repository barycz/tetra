#pragma once

#include "desktop_entry.hpp"
#include "window.hpp"

#include <memory>
#include <string>
#include <vector>

namespace tetra::shell {

class Shell;

// The application launcher: a search field and a grid of apps, shown above
// the bar (Super tap or the logo button). Enter or click launches; Escape or
// a click outside closes it.
class Launcher {
public:
    explicit Launcher(Shell& shell);

private:
    void draw();
    void update_matches();
    void launch(const DesktopEntry& entry);

    Shell& shell_;
    std::unique_ptr<ui::LayerWindow> window_;
    std::vector<const DesktopEntry*> all_;
    std::vector<const DesktopEntry*> matches_;
    std::string query_;
    std::string last_query_;
    int selected_ = 0;
    bool first_frame_ = true;
};

} // namespace tetra::shell
