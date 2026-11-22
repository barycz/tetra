#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tetra {

class Server;
class View;

// The task bar's slot model: 12 slots in 3 groups of 4, mapped to Super+F1..F12.
//
// - Pinned apps own fixed slots (in config order).
// - All windows of an app share one slot.
// - A new app takes the first free slot; when none is free it joins the
//   rightmost unpinned slot (the overflow stack). At most 11 pins guarantee
//   that slot exists.
// - Nothing moves while open: a window keeps its slot until it closes.
class Taskbar {
public:
    static constexpr std::size_t kSlots = 12;

    enum StateFlags : uint32_t { Focused = 1, Minimized = 2 };
    struct SlotState {
        std::string app_id; // desktop ID (or app_id) of the slot's most recent app
        std::string title;
        bool pinned = false;
        uint32_t windows = 0;
        uint32_t state = 0;
        bool operator==(const SlotState&) const = default;
    };
    using Snapshot = std::array<SlotState, kSlots>;

    explicit Taskbar(Server& server);

    void set_pins(const std::vector<std::string>& pins);

    void add_view(View& view);
    void remove_view(View& view);
    void view_focused(View& view);
    // Title, minimized or focus changed somewhere: republish.
    void update();

    // Super+F(index+1): launch, focus, cycle or minimize.
    void activate(std::size_t index);
    // Super+Shift+F(index+1): new instance of the slot's app.
    void launch(std::size_t index);

    [[nodiscard]] Snapshot snapshot() const;
    std::function<void(const Snapshot&)> on_changed;

private:
    struct Slot {
        std::string pin;          // canonical key of the pinned app, or empty
        std::string app;          // key of the app that claimed this slot
        std::vector<View*> views; // most recently focused first
    };

    // Canonical key for an app: its desktop file ID if known, else the
    // app_id itself, so "XTerm" (WM_CLASS) and "debian-xterm" match.
    [[nodiscard]] std::string canonical(std::string_view app_id) const;
    [[nodiscard]] std::size_t place(const std::string& key) const;

    Server& server_;
    std::array<Slot, kSlots> slots_;
    std::unordered_map<View*, std::size_t> slot_of_;
    std::unordered_map<View*, std::string> key_of_;
};

} // namespace tetra
