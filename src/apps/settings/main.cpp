// tetra-settings: edits ~/.config/tetra/config.json.
//
//   tetra-settings [PAGE]     appearance, taskbar, keyboard, power, apps, about

#include "app.hpp"
#include "settings.hpp"

#include <clocale>
#include <print>
#include <span>

int main(int argc, char* argv[]) {
    std::setlocale(LC_ALL, "");
    auto created = tetra::ui::App::create("tetra-settings");
    if (!created) {
        std::println(stderr, "tetra-settings: {}", created.error());
        return 1;
    }
    tetra::ui::App& app = **created;
    int code = 0;
    {
        tetra::settings::Settings settings(app);
        const auto args = std::span(argv, static_cast<std::size_t>(argc));
        if (args.size() > 1) {
            settings.show_page(args[1]);
        }
        code = app.run();
    }
    return code;
}
