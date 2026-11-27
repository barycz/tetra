// tetra-greeter: the login screen. greetd runs `tetra --greeter`, which
// starts this; it exits once greetd has started the chosen session.

#include "app.hpp"
#include "greeter.hpp"

#include <clocale>
#include <csignal>
#include <print>

int main() {
    std::setlocale(LC_ALL, "");
    std::signal(SIGPIPE, SIG_IGN);

    auto created = tetra::ui::App::create("tetra-greeter");
    if (!created) {
        std::println(stderr, "tetra-greeter: {}", created.error());
        return 1;
    }
    tetra::ui::App& app = **created;
    if (app.layer_shell() == nullptr) {
        std::println(stderr, "tetra-greeter: the compositor does not support wlr-layer-shell");
        return 1;
    }

    int code = 0;
    {
        tetra::greeter::Greeter greeter(app);
        code = app.run();
    }
    return code;
}
