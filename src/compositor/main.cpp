#include "log.hpp"
#include "server.hpp"

#include <cstdio>
#include <print>
#include <span>
#include <string_view>
#include <utility>

namespace {

void print_usage(std::FILE* stream) {
    std::println(stream, "Usage: tetra [-s COMMAND] [-d]\n"
                         "\n"
                         "  -s, --startup COMMAND  run COMMAND (via /bin/sh) once the compositor is up\n"
                         "  -d, --debug            verbose logging\n"
                         "  -h, --help             show this help");
}

} // namespace

int main(int argc, char* argv[]) {
    const auto args = std::span(argv, static_cast<std::size_t>(argc)).subspan(1);
    tetra::Options options;
    wlr_log_importance verbosity = WLR_INFO;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "-s" || arg == "--startup") {
            if (++i == args.size()) {
                print_usage(stderr);
                return 2;
            }
            options.startup_command = args[i];
        } else if (arg == "-d" || arg == "--debug") {
            verbosity = WLR_DEBUG;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(stdout);
            return 0;
        } else {
            print_usage(stderr);
            return 2;
        }
    }

    wlr_log_init(verbosity, nullptr);

    auto server = tetra::Server::create(std::move(options));
    if (!server) {
        tetra::log::error("{}", server.error());
        return 1;
    }
    if (auto result = (*server)->run(); !result) {
        tetra::log::error("{}", result.error());
        return 1;
    }
    return 0;
}
