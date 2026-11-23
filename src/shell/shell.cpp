#include "shell.hpp"

#include "bar.hpp"
#include "launcher.hpp"
#include "process.hpp"

#include "tetra-shell-unstable-v1-protocol.h"

#include <chrono>
#include <print>

namespace tetra::shell {

namespace {

using namespace std::chrono;

milliseconds until_next_minute() {
    const auto into_minute =
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()) % minutes(1);
    return minutes(1) - into_minute + milliseconds(50);
}

constexpr auto kTrayInterval = seconds(5);

} // namespace

struct ShellListener {
    static const tetra_shell_v1_listener listener;
};

const tetra_shell_v1_listener ShellListener::listener = {
    .slot =
        [](void* data, tetra_shell_v1* /*shell*/, uint32_t index, const char* app_id,
           const char* title, uint32_t pinned, uint32_t windows, uint32_t state) {
            auto* self = static_cast<Shell*>(data);
            if (index >= Shell::kSlots) {
                return;
            }
            Slot& slot = self->pending_.at(index);
            slot.app_id = app_id;
            slot.title = title;
            slot.pinned = pinned != 0;
            slot.windows = windows;
            slot.focused = (state & TETRA_SHELL_V1_SLOT_STATE_FOCUSED) != 0;
            slot.minimized = (state & TETRA_SHELL_V1_SLOT_STATE_MINIMIZED) != 0;
            self->resolve(slot);
        },
    .done =
        [](void* data, tetra_shell_v1* /*shell*/) {
            auto* self = static_cast<Shell*>(data);
            self->slots_ = self->pending_;
            self->redraw_bars();
        },
    .show_launcher = [](void* data, tetra_shell_v1* /*shell*/) {
        static_cast<Shell*>(data)->toggle_launcher();
    },
};

Shell::Shell(ui::App& app) : app_(app), icons_(app) {
    applications_.load();
    tray_.refresh();

    protocol_ = static_cast<tetra_shell_v1*>(app_.bind_global(&tetra_shell_v1_interface, 1));
    if (protocol_ != nullptr) {
        tetra_shell_v1_add_listener(protocol_, &ShellListener::listener, this);
    } else {
        std::println(stderr, "tetra-shell: not started by tetra; task bar disabled");
    }

    app_.on_output_added = [this](const ui::OutputInfo& output) {
        bars_[output.global] = std::make_unique<Bar>(*this, output);
    };
    app_.on_output_removed = [this](const ui::OutputInfo& output) { bars_.erase(output.global); };
    app_.on_config_changed = [this] { redraw_bars(); };

    dbus_ = ui::DBusConnection::session(app_);
    if (dbus_) {
        try {
            notifications_ = std::make_unique<Notifications>(*this, dbus_->connection());
        } catch (const sdbus::Error& e) {
            std::println(stderr, "tetra-shell: notifications disabled: {}", e.what());
        }
    }

    schedule_clock_tick();
    schedule_tray_refresh();
}

Shell::~Shell() {
    notifications_.reset();
    dbus_.reset();
    launcher_.reset();
    bars_.clear();
    if (protocol_ != nullptr) {
        tetra_shell_v1_destroy(protocol_);
    }
}

void Shell::resolve(Slot& slot) const {
    slot.icon.clear();
    slot.name = slot.app_id;
    if (const DesktopEntry* entry = applications_.find(slot.app_id)) {
        slot.icon = entry->icon;
        slot.name = entry->name;
    } else if (const DesktopEntry* match = applications_.match_app_id(slot.app_id)) {
        slot.icon = match->icon;
        slot.name = match->name;
    } else {
        slot.icon = slot.app_id; // many apps install an icon named after their app_id
    }
}

void Shell::activate_slot(std::size_t index) {
    if (protocol_ != nullptr) {
        tetra_shell_v1_activate_slot(protocol_, static_cast<uint32_t>(index));
    }
}

void Shell::launch_slot(std::size_t index) {
    if (protocol_ != nullptr) {
        tetra_shell_v1_launch_slot(protocol_, static_cast<uint32_t>(index));
    }
}

void Shell::launch(const DesktopEntry& entry) {
    const auto argv = exec_argv(entry, app_.config().terminal);
    if (!spawn_detached(argv)) {
        std::println(stderr, "tetra-shell: cannot start '{}'", argv.empty() ? entry.id : argv.front());
    }
}

void Shell::toggle_launcher() {
    if (launcher_) {
        hide_launcher();
        return;
    }
    applications_.load(); // pick up newly installed apps
    launcher_ = std::make_unique<Launcher>(*this);
}

void Shell::hide_launcher() {
    if (!launcher_) {
        return;
    }
    // Never destroy a window from inside its own draw callback: hand it to a
    // zero-delay timer, which drops it on the next loop iteration.
    std::shared_ptr<Launcher> closing(std::move(launcher_));
    app_.add_timer(milliseconds(0), [closing] {});
}

void Shell::redraw_bars() {
    for (auto& [global, bar] : bars_) {
        bar->redraw();
    }
}

void Shell::schedule_clock_tick() {
    app_.add_timer(until_next_minute(), [this] {
        redraw_bars();
        schedule_clock_tick();
    });
}

void Shell::schedule_tray_refresh() {
    app_.add_timer(duration_cast<milliseconds>(kTrayInterval), [this] {
        if (tray_.refresh()) {
            redraw_bars();
        }
        schedule_tray_refresh();
    });
}

} // namespace tetra::shell
