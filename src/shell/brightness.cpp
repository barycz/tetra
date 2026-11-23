#include "brightness.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <print>

namespace tetra::shell {

namespace {

namespace fs = std::filesystem;

int read_int(const fs::path& path) {
    std::ifstream file(path);
    std::string text;
    std::getline(file, text);
    int value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

} // namespace

Brightness::Brightness(sdbus::IConnection* system_bus) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/sys/class/backlight", ec)) {
        const int max = read_int(entry.path() / "max_brightness");
        if (max > 0) {
            device_ = entry.path();
            name_ = entry.path().filename().string();
            max_ = max;
            break;
        }
    }
    if (max_ == 0 || system_bus == nullptr) {
        return;
    }
    try {
        session_ = sdbus::createProxy(*system_bus, sdbus::ServiceName{"org.freedesktop.login1"},
                                      sdbus::ObjectPath{"/org/freedesktop/login1/session/auto"});
    } catch (const sdbus::Error& e) {
        std::println(stderr, "tetra-shell: brightness control unavailable: {}", e.what());
    }
}

float Brightness::level() const {
    if (max_ == 0) {
        return 0;
    }
    return static_cast<float>(read_int(device_ / "brightness")) / static_cast<float>(max_);
}

float Brightness::change(float delta) {
    if (!available()) {
        return 0;
    }
    // Snap to 5% steps; never fully dark (a black screen looks broken).
    const float target = std::clamp(std::round((level() + delta) * 20.0F) / 20.0F, 0.05F, 1.0F);
    const auto value = static_cast<uint32_t>(std::lround(target * static_cast<float>(max_)));
    try {
        session_->callMethod("SetBrightness")
            .onInterface("org.freedesktop.login1.Session")
            .withArguments(std::string("backlight"), name_, value);
    } catch (const sdbus::Error& e) {
        std::println(stderr, "tetra-shell: cannot set brightness: {}", e.what());
    }
    return target;
}

} // namespace tetra::shell
