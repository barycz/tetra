#pragma once

#include "wlr.hpp"

#include <format>
#include <string>
#include <utility>

// std::format front end for wlroots' logger, so our messages share its
// timestamps, verbosity and destination.
namespace tetra::log {

template <typename... Args>
void write(wlr_log_importance level, std::format_string<Args...> fmt, Args&&... args) {
    if (level > wlr_log_get_verbosity()) {
        return;
    }
    const std::string message = std::format(fmt, std::forward<Args>(args)...);
    _wlr_log(level, "[tetra] %s", message.c_str());
}

template <typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    write(WLR_ERROR, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    write(WLR_INFO, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    write(WLR_DEBUG, fmt, std::forward<Args>(args)...);
}

} // namespace tetra::log
