#pragma once

#include "app.hpp"
#include "file_dialog.hpp"
#include "icons.hpp"
#include "window.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tetra::portal {

struct AccessOptions {
    std::string title;    // "Allow Firefox to take screenshots?"
    std::string subtitle; // a line under the title
    std::string body;     // more detail, dimmed
    std::string icon;     // an icon name, may be empty
    std::string grant_label;
    std::string deny_label;
    std::vector<Choice> choices;
};

// org.freedesktop.impl.portal.Access: a small window asking the user to
// allow or deny something (xdg-desktop-portal uses it e.g. before an app
// may take screenshots without the user picking them). `done` gets true
// for Allow, and the choices; it runs once, not after cancel().
class AccessDialog {
public:
    AccessDialog(ui::App& app, AccessOptions options, std::function<void(bool, std::vector<Choice>)> done);
    ~AccessDialog();

    AccessDialog(const AccessDialog&) = delete;
    AccessDialog& operator=(const AccessDialog&) = delete;
    AccessDialog(AccessDialog&&) = delete;
    AccessDialog& operator=(AccessDialog&&) = delete;

    void cancel();

private:
    void draw();
    void finish(bool granted);

    ui::App& app_;
    ui::IconCache icons_;
    AccessOptions options_;
    std::function<void(bool, std::vector<Choice>)> done_;
    std::unique_ptr<ui::ToplevelWindow> window_;
    bool finished_ = false;
};

} // namespace tetra::portal
