// tetra-shell: the desktop shell (bar, and later launcher, tray,
// notifications, OSD, lock screen). Started and restarted by the compositor.

#include "app.hpp"
#include "bar.hpp"

#include <chrono>
#include <clocale>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <print>

namespace {

using namespace std::chrono;

// Time until the next minute boundary (plus a little slack).
milliseconds until_next_minute() {
    const auto now = system_clock::now().time_since_epoch();
    const auto into_minute = duration_cast<milliseconds>(now) % minutes(1);
    return minutes(1) - into_minute + milliseconds(50);
}

} // namespace

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

    std::map<uint32_t, std::unique_ptr<tetra::shell::Bar>> bars; // by wl_output global
    app.on_output_added = [&](const tetra::ui::OutputInfo& output) {
        bars[output.global] = std::make_unique<tetra::shell::Bar>(app, output);
    };
    app.on_output_removed = [&](const tetra::ui::OutputInfo& output) { bars.erase(output.global); };

    std::function<void()> tick = [&] {
        for (auto& [global, bar] : bars) {
            bar->redraw();
        }
        app.add_timer(until_next_minute(), tick);
    };
    app.add_timer(until_next_minute(), tick);

    const int code = app.run();
    bars.clear(); // windows go before the App
    return code;
}
