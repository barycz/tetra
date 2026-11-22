#include "taskbar.hpp"

#include "desktop_entry.hpp"
#include "log.hpp"
#include "process.hpp"
#include "server.hpp"
#include "view.hpp"

#include <algorithm>

namespace tetra {

Taskbar::Taskbar(Server& server) : server_(server) {}

std::string Taskbar::canonical(std::string_view app_id) const {
    if (app_id.empty()) {
        return {};
    }
    if (const DesktopEntry* entry = server_.desktop_entries().match_app_id(app_id)) {
        return entry->id;
    }
    return std::string(app_id);
}

void Taskbar::set_pins(const std::vector<std::string>& pins) {
    for (std::size_t i = 0; i < kSlots; ++i) {
        Slot& slot = slots_.at(i);
        slot.pin = i < pins.size() ? canonical(pins[i]) : std::string{};
        if (slot.views.empty()) {
            slot.app = slot.pin;
        }
    }
    update();
}

std::size_t Taskbar::place(const std::string& key) const {
    if (!key.empty()) {
        // The app's pinned slot, or the slot it already claimed.
        for (std::size_t i = 0; i < kSlots; ++i) {
            if (slots_.at(i).pin == key) {
                return i;
            }
        }
        for (std::size_t i = 0; i < kSlots; ++i) {
            if (!slots_.at(i).views.empty() && slots_.at(i).app == key) {
                return i;
            }
        }
    }
    for (std::size_t i = 0; i < kSlots; ++i) {
        if (slots_.at(i).pin.empty() && slots_.at(i).views.empty()) {
            return i;
        }
    }
    // Overflow: the rightmost unpinned slot.
    for (std::size_t i = kSlots; i-- > 0;) {
        if (slots_.at(i).pin.empty()) {
            return i;
        }
    }
    return kSlots - 1; // unreachable with at most 11 pins
}

void Taskbar::add_view(View& view) {
    if (slot_of_.contains(&view)) {
        return;
    }
    std::string key = canonical(view.app_id());
    const std::size_t index = place(key);
    Slot& slot = slots_.at(index);
    if (slot.views.empty() && slot.pin.empty()) {
        slot.app = key;
    }
    slot.views.insert(slot.views.begin(), &view);
    slot_of_[&view] = index;
    key_of_[&view] = std::move(key);
    update();
}

void Taskbar::remove_view(View& view) {
    auto it = slot_of_.find(&view);
    if (it == slot_of_.end()) {
        return;
    }
    Slot& slot = slots_.at(it->second);
    std::erase(slot.views, &view);
    if (slot.views.empty()) {
        slot.app = slot.pin;
    }
    slot_of_.erase(it);
    key_of_.erase(&view);
    update();
}

void Taskbar::view_focused(View& view) {
    auto it = slot_of_.find(&view);
    if (it == slot_of_.end()) {
        return;
    }
    auto& views = slots_.at(it->second).views;
    std::erase(views, &view);
    views.insert(views.begin(), &view);
    update();
}

void Taskbar::update() {
    if (on_changed) {
        on_changed(snapshot());
    }
}

Taskbar::Snapshot Taskbar::snapshot() const {
    Snapshot snapshot;
    const View* focused = server_.focused_view();
    for (std::size_t i = 0; i < kSlots; ++i) {
        const Slot& slot = slots_.at(i);
        SlotState& state = snapshot.at(i);
        state.pinned = !slot.pin.empty();
        state.windows = static_cast<uint32_t>(slot.views.size());
        if (slot.views.empty()) {
            state.app_id = slot.pin;
            continue;
        }
        View* recent = slot.views.front();
        state.app_id = key_of_.at(recent);
        state.title = std::string(recent->title());
        const bool focused_here = std::ranges::find(slot.views, focused) != slot.views.end();
        const bool all_minimized =
            std::ranges::all_of(slot.views, [](const View* v) { return v->minimized(); });
        state.state = (focused_here ? Focused : 0U) | (all_minimized ? Minimized : 0U);
    }
    return snapshot;
}

void Taskbar::activate(std::size_t index) {
    if (index >= kSlots) {
        return;
    }
    const auto& views = slots_.at(index).views;
    if (views.empty()) {
        if (!slots_.at(index).pin.empty()) {
            launch(index);
        }
        return;
    }
    View* focused = server_.focused_view();
    const bool focused_here = std::ranges::find(views, focused) != views.end();
    if (!focused_here) {
        server_.focus_view(views.front());
    } else if (views.size() == 1) {
        focused->set_minimized(true);
    } else {
        // Least recently used next, so repeated presses visit every window.
        server_.focus_view(views.back());
    }
}

void Taskbar::launch(std::size_t index) {
    if (index >= kSlots) {
        return;
    }
    const Slot& slot = slots_.at(index);
    const std::string& key = !slot.views.empty() ? key_of_.at(slot.views.front()) : slot.pin;
    if (key.empty()) {
        return;
    }
    const DesktopDatabase& db = server_.desktop_entries();
    const DesktopEntry* entry = db.find(key);
    if (entry == nullptr) {
        entry = db.match_app_id(key);
    }
    const auto argv = entry != nullptr ? exec_argv(*entry, server_.config().terminal)
                                       : std::vector<std::string>{key};
    if (!spawn_detached(argv)) {
        log::error("cannot start '{}'", argv.front());
    }
}

} // namespace tetra
