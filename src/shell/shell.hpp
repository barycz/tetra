#pragma once

#include "app.hpp"
#include "audio.hpp"
#include "brightness.hpp"
#include "dbus.hpp"
#include "desktop_entry.hpp"
#include "icons.hpp"
#include "idle.hpp"
#include "lock_screen.hpp"
#include "logind.hpp"
#include "notifications.hpp"
#include "osd.hpp"
#include "polkit_agent.hpp"
#include "power.hpp"
#include "screenshot.hpp"
#include "shell_service.hpp"
#include "sni.hpp"
#include "tray_menu.hpp"
#include "tray.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

struct tetra_shell_v1;

namespace tetra::shell {

class Bar;
class Launcher;
class VolumePopup;

// One task bar slot as published by the compositor.
struct Slot {
    std::string app_id; // desktop ID or app_id; empty if free
    std::string title;
    bool pinned = false;
    uint32_t windows = 0;
    bool focused = false;
    bool minimized = false;
    // Resolved from the desktop database:
    std::string icon;
    std::string name;
};

// The shell process: owns the bars (one per output), the launcher, the tray
// and the connection to the compositor's tetra-shell protocol.
class Shell {
public:
    static constexpr std::size_t kSlots = 12;

    explicit Shell(ui::App& app);
    ~Shell();

    Shell(const Shell&) = delete;
    Shell& operator=(const Shell&) = delete;
    Shell(Shell&&) = delete;
    Shell& operator=(Shell&&) = delete;

    [[nodiscard]] ui::App& app() noexcept { return app_; }
    [[nodiscard]] ui::IconCache& icons() noexcept { return icons_; }
    [[nodiscard]] const DesktopDatabase& applications() const noexcept { return applications_; }
    [[nodiscard]] const std::array<Slot, kSlots>& slots() const noexcept { return slots_; }
    [[nodiscard]] const Tray& tray() const noexcept { return tray_; }
    [[nodiscard]] bool connected() const noexcept { return protocol_ != nullptr; }

    [[nodiscard]] Audio& audio() noexcept { return *audio_; }
    [[nodiscard]] Notifications* notifications() noexcept { return notifications_.get(); }
    [[nodiscard]] const ui::OutputInfo* output_info(const wl_output* output) const;
    [[nodiscard]] SniTray* sni() noexcept { return sni_.get(); }
    [[nodiscard]] Screenshots& screenshots() noexcept { return *screenshots_; }
    // Asks the compositor for a screenshot (tetra_shell_v1.take_screenshot);
    // false if it cannot take the request.
    bool request_screenshot(uint32_t mode);
    // A tray item's menu, above the bar at `anchor_x` on `output`.
    void open_tray_menu(const SniItem& item, wl_output* output, float anchor_x);
    void hide_tray_menu();

    void activate_slot(std::size_t index);
    void launch_slot(std::size_t index);
    void toggle_launcher();
    void hide_launcher();
    void toggle_volume_popup();
    void hide_volume_popup();
    void launch(const DesktopEntry& entry);
    // Locks the session (Super+L, idle, logind, before sleep).
    void lock();
    // Ends the session (the compositor exits); false if it cannot be asked.
    bool log_out();
    // Suspend, restart, shut down (logind).
    [[nodiscard]] ui::Power& power() noexcept { return *power_; }
    void redraw_bars();
    // Volume/brightness change from a key or the mouse wheel: apply and show the OSD.
    void handle_media_key(uint32_t key);
    void step_volume(float delta);

    // Symbolic icon for the current volume level.
    [[nodiscard]] static const char* volume_icon(const Audio::State& state);

private:
    friend struct ShellListener;
    void resolve(Slot& slot) const;
    void schedule_tray_refresh();
    void schedule_clock_tick();
    void apply_idle_config();

    ui::App& app_;
    ui::IconCache icons_;
    DesktopDatabase applications_;
    Tray tray_;
    tetra_shell_v1* protocol_ = nullptr;
    std::array<Slot, kSlots> slots_;
    std::array<Slot, kSlots> pending_; // until the next done event
    std::unique_ptr<Audio> audio_;
    std::map<uint32_t, std::unique_ptr<Bar>> bars_; // by wl_output global
    std::unique_ptr<Launcher> launcher_;
    std::unique_ptr<VolumePopup> volume_popup_;
    std::unique_ptr<Osd> osd_;
    std::map<uint32_t, ui::OutputInfo> outputs_; // by wl_output global
    std::unique_ptr<LockScreen> lock_screen_;
    std::unique_ptr<Idle> idle_;
    std::unique_ptr<Screenshots> screenshots_;
    // Bus services; declared after everything they use, so they go first.
    std::unique_ptr<ui::DBusConnection> dbus_;
    std::unique_ptr<ui::DBusConnection> system_bus_;
    std::unique_ptr<Brightness> brightness_;
    std::unique_ptr<Logind> logind_;
    std::unique_ptr<ui::Power> power_;
    std::unique_ptr<PolkitAgent> polkit_;
    std::unique_ptr<Notifications> notifications_;
    std::unique_ptr<SniTray> sni_;
    std::unique_ptr<TrayMenu> tray_menu_;
    std::unique_ptr<ShellService> service_;
};

} // namespace tetra::shell
