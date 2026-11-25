// tetra-terminal: a terminal emulator (libvterm, drawn with ImGui).
//
//   tetra-terminal [--app-id ID] [--title TITLE] [-e] [COMMAND [ARG...]]
//
// Without a command it runs the user's shell. As `terminal` in config.json
// it also opens Terminal=true applications (the command follows).

#include "app.hpp"
#include "terminal.hpp"

#include <clocale>
#include <print>
#include <span>
#include <string_view>

int main(int argc, char* argv[]) {
    std::setlocale(LC_ALL, "");
    const auto args = std::span(argv, static_cast<std::size_t>(argc)).subspan(1);
    std::string app_id = "tetra-terminal";
    tetra::terminal::Terminal::Options options;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if ((arg == "--app-id" || arg == "--title") && i + 1 < args.size()) {
            (arg == "--app-id" ? app_id : options.title) = args[++i];
        } else if (arg == "-h" || arg == "--help") {
            std::println("usage: tetra-terminal [--app-id ID] [--title TITLE] [-e] [COMMAND [ARG...]]");
            return 0;
        } else {
            // The command: everything from here (after an optional -e or --).
            if (arg == "-e" || arg == "--") {
                ++i;
            }
            options.command.assign(args.begin() + static_cast<std::ptrdiff_t>(i), args.end());
            break;
        }
    }

    auto created = tetra::ui::App::create(app_id);
    if (!created) {
        std::println(stderr, "tetra-terminal: {}", created.error());
        return 1;
    }
    tetra::ui::App& app = **created;
    int code = 0;
    {
        tetra::terminal::Terminal terminal(app, std::move(options));
        if (!terminal.started()) {
            return 1;
        }
        code = app.run();
    }
    return code;
}
