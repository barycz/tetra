#include "shell.hpp"

#include "bar.hpp"
#include "launcher.hpp"
#include "process.hpp"
#include "protocols.hpp"
#include "volume_popup.hpp"

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
    .media_key = [](void* data, tetra_shell_v1* /*shell*/,
                    uint32_t key) { static_cast<Shell*>(data)->handle_media_key(key); },
    .lock = [](void* data, tetra_shell_v1* /*shell*/) { static_cast<Shell*>(data)->lock(); },
    .screenshot =
        [](void* data, tetra_shell_v1* /*shell*/, uint32_t serial, uint32_t mode, wl_output* output,
           int32_t x, int32_t y, int32_t width, int32_t height) {
            auto* self = static_cast<Shell*>(data);
            if (output != nullptr && !self->lock_screen_->active()) {
                self->screenshots_->take(serial, mode, output, x, y, width, height);
            }
        },
};

Shell::Shell(ui::App& app) : app_(app), icons_(app) {
    applications_.load();
    tray_.refresh();

    audio_ = std::make_unique<Audio>(app_);
    audio_->on_changed = [this] {
        redraw_bars();
        if (volume_popup_) {
            volume_popup_->redraw();
        }
    };
    osd_ = std::make_unique<Osd>(*this);

    protocol_ = static_cast<tetra_shell_v1*>(app_.bind_global(&tetra_shell_v1_interface, 4));
    if (protocol_ != nullptr) {
        tetra_shell_v1_add_listener(protocol_, &ShellListener::listener, this);
    } else {
        std::println(stderr, "tetra-shell: not started by tetra; task bar disabled");
    }

    lock_screen_ = std::make_unique<LockScreen>(
        *this, static_cast<ext_session_lock_manager_v1*>(
                   app_.bind_global(&ext_session_lock_manager_v1_interface, 1)));
    lock_screen_->on_changed = [this](bool locked) {
        if (locked && logind_) {
            logind_->release_sleep_inhibitor(); // the screen is covered: sleep may go ahead
        }
        apply_idle_config();
    };
    idle_ = std::make_unique<Idle>(
        app_.seat(),
        static_cast<ext_idle_notifier_v1*>(app_.bind_global(&ext_idle_notifier_v1_interface, 1)),
        static_cast<zwlr_output_power_manager_v1*>(
            app_.bind_global(&zwlr_output_power_manager_v1_interface, 1)));
    idle_->on_lock = [this] { lock(); };
    screenshots_ = std::make_unique<Screenshots>(*this);

    app_.on_output_added = [this](const ui::OutputInfo& output) {
        outputs_[output.global] = output;
        bars_[output.global] = std::make_unique<Bar>(*this, output);
        lock_screen_->add_output(output);
        idle_->add_output(output.global, output.output);
    };
    app_.on_output_removed = [this](const ui::OutputInfo& output) {
        idle_->remove_output(output.global);
        lock_screen_->remove_output(output);
        bars_.erase(output.global);
        outputs_.erase(output.global);
    };
    app_.on_config_changed = [this] {
        redraw_bars();
        apply_idle_config();
    };

    dbus_ = ui::DBusConnection::session(app_);
    if (dbus_) {
        try {
            notifications_ = std::make_unique<Notifications>(*this, dbus_->connection());
        } catch (const sdbus::Error& e) {
            std::println(stderr, "tetra-shell: notifications disabled: {}", e.what());
        }
        try {
            sni_ = std::make_unique<SniTray>(app_, dbus_->connection());
            sni_->on_changed = [this] { redraw_bars(); };
        } catch (const sdbus::Error& e) {
            std::println(stderr, "tetra-shell: app tray icons disabled: {}", e.what());
        }
    }
    system_bus_ = ui::DBusConnection::system(app_);
    brightness_ = std::make_unique<Brightness>(system_bus_ ? &system_bus_->connection() : nullptr);
    if (system_bus_) {
        logind_ = std::make_unique<Logind>(system_bus_->connection());
        logind_->on_lock = [this] { lock(); };
        try {
            polkit_ = std::make_unique<PolkitAgent>(*this, system_bus_->connection(),
                                                    logind_->session_id());
        } catch (const sdbus::Error& e) {
            std::println(stderr, "tetra-shell: polkit agent disabled: {}", e.what());
        }
    }
    apply_idle_config();

    schedule_clock_tick();
    schedule_tray_refresh();
}

Shell::~Shell() {
    tray_menu_.reset();
    sni_.reset();
    notifications_.reset();
    polkit_.reset();
    logind_.reset();
    brightness_.reset();
    system_bus_.reset();
    dbus_.reset();
    osd_.reset();
    screenshots_.reset();
    idle_.reset();
    lock_screen_.reset();
    volume_popup_.reset();
    launcher_.reset();
    bars_.clear();
    audio_.reset();
    if (protocol_ != nullptr) {
        tetra_shell_v1_destroy(protocol_);
    }
}

const char* Shell::volume_icon(const Audio::State& state) {
    if (!state.available || state.muted || state.volume < 0.005F) {
        return "audio-volume-muted-symbolic";
    }
    if (state.volume < 0.34F) {
        return "audio-volume-low-symbolic";
    }
    return state.volume < 0.67F ? "audio-volume-medium-symbolic" : "audio-volume-high-symbolic";
}

void Shell::step_volume(float delta) {
    if (!audio_->state().available) {
        return;
    }
    audio_->change_volume(delta);
    const Audio::State& state = audio_->state();
    osd_->show(volume_icon(state), state.volume, state.muted);
}

void Shell::handle_media_key(uint32_t key) {
    constexpr float kStep = 0.05F;
    switch (key) {
    case TETRA_SHELL_V1_MEDIA_KEY_VOLUME_UP:
        step_volume(kStep);
        break;
    case TETRA_SHELL_V1_MEDIA_KEY_VOLUME_DOWN:
        step_volume(-kStep);
        break;
    case TETRA_SHELL_V1_MEDIA_KEY_MUTE:
        if (audio_->state().available) {
            audio_->toggle_mute();
            const Audio::State& state = audio_->state();
            osd_->show(volume_icon(state), state.volume, state.muted);
        }
        break;
    case TETRA_SHELL_V1_MEDIA_KEY_MIC_MUTE:
        audio_->toggle_mic_mute();
        break;
    case TETRA_SHELL_V1_MEDIA_KEY_BRIGHTNESS_UP:
    case TETRA_SHELL_V1_MEDIA_KEY_BRIGHTNESS_DOWN:
        if (brightness_ && brightness_->available()) {
            const float level =
                brightness_->change(key == TETRA_SHELL_V1_MEDIA_KEY_BRIGHTNESS_UP ? kStep : -kStep);
            osd_->show("display-brightness-symbolic", level, false);
        }
        break;
    default:
        break;
    }
}

void Shell::toggle_volume_popup() {
    if (volume_popup_) {
        hide_volume_popup();
        return;
    }
    volume_popup_ = std::make_unique<VolumePopup>(*this);
}

void Shell::hide_volume_popup() {
    if (!volume_popup_) {
        return;
    }
    std::shared_ptr<VolumePopup> closing(std::move(volume_popup_));
    app_.add_timer(milliseconds(0), [closing] {});
}

const ui::OutputInfo* Shell::output_info(const wl_output* output) const {
    for (const auto& [global, info] : outputs_) {
        if (info.output == output) {
            return &info;
        }
    }
    return nullptr;
}

void Shell::open_tray_menu(const SniItem& item, wl_output* output, float anchor_x) {
    hide_tray_menu();
    if (item.menu_path.empty() || !dbus_) {
        return;
    }
    try {
        tray_menu_ = std::make_unique<TrayMenu>(*this, dbus_->connection(), item.service,
                                                item.menu_path, output, anchor_x);
    } catch (const sdbus::Error& e) {
        std::println(stderr, "tetra-shell: tray menu: {}", e.what());
    }
}

void Shell::hide_tray_menu() {
    if (tray_menu_) {
        // Possibly called from the menu's own draw or a reply handler of its
        // proxy: destroy it on the next turn.
        std::shared_ptr<TrayMenu> closing(std::move(tray_menu_));
        app_.add_timer(milliseconds(0), [closing] {});
    }
}

void Shell::lock() {
    if (lock_screen_->locked()) {
        if (logind_) {
            logind_->release_sleep_inhibitor(); // already covered: let sleep go ahead
        }
        return;
    }
    hide_launcher();
    hide_volume_popup();
    hide_tray_menu();
    lock_screen_->lock(outputs_);
}

void Shell::apply_idle_config() {
    const config::Idle& idle = app_.config().idle;
    idle_->configure(idle, lock_screen_->active());
    if (logind_) {
        logind_->set_lock_before_sleep(idle.lock_before_sleep);
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
        lock_screen_->redraw();
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
