// tetra-shell: the desktop shell (bar with task slots, launcher, tray; later
// notifications, OSD, lock screen). Started and restarted by the compositor.

#include "app.hpp"
#include "shell.hpp"

#include <clocale>
#include <print>

int main() {
    std::setlocale(LC_ALL, "");

    auto created = tetra::ui::App::create("tetra-shell");
    if (!created) {
        std::println(stderr, "tetra-shell: {}", created.error());
        return 1;
    }
    tetra::ui::App& app = **created;
    if (app.layer_shell() == nullptr) {
        std::println(stderr, "tetra-shell: the compositor does not support wlr-layer-shell");
        return 1;
    }

    int code = 0;
    {
        tetra::shell::Shell shell(app); // windows and textures go before the App
        code = app.run();
    }
    return code;
}
