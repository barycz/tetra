#pragma once

#include "app.hpp"
#include "desktop_entry.hpp"
#include "icons.hpp"
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

    void activate_slot(std::size_t index);
    void launch_slot(std::size_t index);
    void toggle_launcher();
    void hide_launcher();
    void launch(const DesktopEntry& entry);
    void redraw_bars();

private:
    friend struct ShellListener;
    void resolve(Slot& slot) const;
    void schedule_tray_refresh();
    void schedule_clock_tick();

    ui::App& app_;
    ui::IconCache icons_;
    DesktopDatabase applications_;
    Tray tray_;
    tetra_shell_v1* protocol_ = nullptr;
    std::array<Slot, kSlots> slots_;
    std::array<Slot, kSlots> pending_; // until the next done event
    std::map<uint32_t, std::unique_ptr<Bar>> bars_; // by wl_output global
    std::unique_ptr<Launcher> launcher_;
};

} // namespace tetra::shell
