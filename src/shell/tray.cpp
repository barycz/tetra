#include "tray.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

namespace tetra::shell {

namespace {

namespace fs = std::filesystem;

std::string read_line(const fs::path& path) {
    std::ifstream file(path);
    std::string line;
    std::getline(file, line);
    return line;
}

std::optional<int> read_int(const fs::path& path) {
    const std::string text = read_line(path);
    int value = 0;
    if (std::from_chars(text.data(), text.data() + text.size(), value).ec != std::errc{}) {
        return std::nullopt;
    }
    return value;
}

// Link quality (0-100) of `interface` from /proc/net/wireless.
std::optional<int> wireless_quality(const std::string& interface) {
    std::ifstream file("/proc/net/wireless");
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        std::string name;
        std::string status;
        double quality = 0;
        if (fields >> name >> status >> quality && name == interface + ":") {
            return std::clamp(static_cast<int>(quality * 100 / 70), 0, 100);
        }
    }
    return std::nullopt;
}

void read_network(Tray::State& state) {
    bool wired_up = false;
    bool has_wireless = false;
    std::optional<int> wifi_quality;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/sys/class/net", ec)) {
        const auto& dir = entry.path();
        if (!fs::exists(dir / "device")) {
            continue; // loopback, bridges, VPNs, containers
        }
        const bool wireless = fs::exists(dir / "wireless") || fs::exists(dir / "phy80211");
        const bool up = read_line(dir / "operstate") == "up";
        has_wireless = has_wireless || wireless;
        if (wireless && up) {
            wifi_quality = wireless_quality(dir.filename().string()).value_or(50);
        } else if (!wireless && up) {
            wired_up = true;
        }
    }

    if (wired_up) {
        state.network_icon = "network-wired-symbolic";
        state.network_label = "Wired connection";
    } else if (wifi_quality) {
        const int q = *wifi_quality;
        const char* level = q > 75 ? "excellent" : q > 50 ? "good" : q > 25 ? "ok" : "weak";
        state.network_icon = std::format("network-wireless-signal-{}-symbolic", level);
        state.network_label = std::format("Wi-Fi, signal {}%", q);
    } else {
        state.network_icon =
            has_wireless ? "network-wireless-offline-symbolic" : "network-wired-disconnected-symbolic";
        state.network_label = "Disconnected";
    }
}

void read_battery(Tray::State& state) {
    state.battery_percent.reset();
    state.battery_icon.clear();
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/sys/class/power_supply", ec)) {
        const auto& dir = entry.path();
        // Skip peripherals (mice, headsets) that report scope "Device".
        if (read_line(dir / "type") != "Battery" || read_line(dir / "scope") == "Device") {
            continue;
        }
        const auto capacity = read_int(dir / "capacity");
        if (!capacity) {
            continue;
        }
        const std::string status = read_line(dir / "status");
        const int percent = std::clamp(*capacity, 0, 100);
        const int level = percent / 10 * 10;
        state.battery_percent = percent;
        if (status == "Full" || (status == "Charging" && level == 100)) {
            state.battery_icon = "battery-level-100-charged-symbolic";
        } else if (status == "Charging") {
            state.battery_icon = std::format("battery-level-{}-charging-symbolic", level);
        } else {
            state.battery_icon = std::format("battery-level-{}-symbolic", level);
        }
        return; // first system battery
    }
}

} // namespace

bool Tray::refresh() {
    State next;
    read_network(next);
    read_battery(next);
    if (next == state_) {
        return false;
    }
    state_ = std::move(next);
    return true;
}

} // namespace tetra::shell
