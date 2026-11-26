// tetra-monitor: processes, resource use and file systems.
//
//   tetra-monitor [--search TEXT] [processes|resources|filesystems]

#include "app.hpp"
#include "monitor.hpp"

#include <clocale>
#include <print>
#include <span>
#include <string_view>

int main(int argc, char* argv[]) {
    std::setlocale(LC_ALL, "");
    auto created = tetra::ui::App::create("tetra-monitor");
    if (!created) {
        std::println(stderr, "tetra-monitor: {}", created.error());
        return 1;
    }
    tetra::ui::App& app = **created;
    int code = 0;
    {
        tetra::monitor::Monitor monitor(app);
        const auto args = std::span(argv, static_cast<std::size_t>(argc)).subspan(1);
        for (std::size_t i = 0; i < args.size(); ++i) {
            const std::string_view arg = args[i];
            if (arg == "--search" && i + 1 < args.size()) {
                monitor.set_search(args[++i]);
            } else if (arg == "-h" || arg == "--help") {
                std::println("usage: tetra-monitor [--search TEXT] [processes|resources|filesystems]");
                return 0;
            } else {
                monitor.show_page(arg);
            }
        }
        code = app.run();
    }
    return code;
}
