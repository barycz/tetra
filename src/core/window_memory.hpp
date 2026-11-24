#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Where application windows were last, so they open there again
// (~/.local/state/tetra/windows.json). This is state, not configuration:
// the compositor writes it as windows move.
//
// Each app (app_id, or WM_CLASS for X11) has numbered window slots: its first
// open window uses slot 0, a second one at the same time slot 1, and so on
// (the lowest free slot). A slot remembers one place per display, so a
// window goes back to where it was on whichever display it opens on, and
// which display it was on for each set of connected displays ("setup"), so
// plugging a monitor back in brings windows back to it.
namespace tetra::windows {

// A window's content area relative to its display's top-left corner, in
// layout (logical) pixels. When maximized, the size and position it
// returns to.
struct Place {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool maximized = false;
    bool operator==(const Place&) const = default;
};

class Memory {
public:
    static constexpr std::size_t kMaxSlots = 16; // per app; more windows are not remembered
    static constexpr std::size_t kMaxApps = 200; // the least recently used are forgotten

    // An unreadable file gives an empty memory and an error message.
    static Memory parse(std::string_view text, std::string* error = nullptr);
    [[nodiscard]] std::string dump() const;

    // Takes the lowest slot not held by another open window of the app; none
    // when all kMaxSlots are held. release() gives it back.
    std::optional<std::size_t> claim(const std::string& app);
    void release(const std::string& app, std::size_t slot);

    // The display a window in this slot should open on, among `connected`
    // (display keys): the one it was on with these same displays, else the
    // last one it was on if connected. None: the slot has no memory of them.
    [[nodiscard]] std::optional<std::string> display(const std::string& app, std::size_t slot,
                                                     std::span<const std::string> connected) const;
    // Its place on that display, if it was ever there.
    [[nodiscard]] std::optional<Place> place(const std::string& app, std::size_t slot,
                                             const std::string& display) const;
    // Its place on the display it was on last (for a size on a new display).
    [[nodiscard]] std::optional<Place> last_place(const std::string& app, std::size_t slot) const;

    // The window is now at `place` on `display`; `connected` is the setup.
    // `now` (seconds) orders apps for forgetting.
    void remember(const std::string& app, std::size_t slot, std::span<const std::string> connected,
                  const std::string& display, const Place& place, std::int64_t now);

    // Replaces what is remembered (slots held by open windows stay held).
    void replace(Memory other);
    [[nodiscard]] bool empty() const noexcept { return apps_.empty(); }

private:
    struct Slot {
        std::string display;                         // the last one
        std::map<std::string, std::string> setups;   // setup -> display
        std::map<std::string, Place> places;         // display -> place
    };
    struct App {
        std::int64_t used = 0;
        std::vector<Slot> slots;
    };
    [[nodiscard]] const Slot* find(const std::string& app, std::size_t slot) const;

    std::map<std::string, App> apps_;
    std::map<std::string, std::vector<bool>> held_; // open windows, not saved
};

// The set of connected displays as one key (sorted, so order does not matter).
std::string setup_key(std::span<const std::string> connected);

// $XDG_STATE_HOME/tetra/windows.json, falling back to ~/.local/state.
std::filesystem::path state_file();

} // namespace tetra::windows
