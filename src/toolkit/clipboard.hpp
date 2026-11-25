#pragma once

#include <wayland-client.h>

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct zwp_primary_selection_device_manager_v1;
struct zwp_primary_selection_device_v1;
struct zwp_primary_selection_offer_v1;
struct zwp_primary_selection_source_v1;

namespace tetra::ui {

class App;

// Text on the clipboard (copy/paste) and the primary selection (select,
// middle click), through wl_data_device and primary-selection-v1.
//
// When another client takes a selection, its text is read at once, so
// text() can answer synchronously (ImGui asks that way). A selection this
// process set is never read back over a pipe: text() already holds it.
class Clipboard {
public:
    enum class Selection : std::uint8_t { Clipboard = 0, Primary = 1 };

    Clipboard(App& app, wl_seat* seat, wl_data_device_manager* data_manager,
              zwp_primary_selection_device_manager_v1* primary_manager);
    ~Clipboard();

    Clipboard(const Clipboard&) = delete;
    Clipboard& operator=(const Clipboard&) = delete;
    Clipboard(Clipboard&&) = delete;
    Clipboard& operator=(Clipboard&&) = delete;

    // Offers `text`; `serial` is that of the input event that asked for it.
    void set(Selection which, std::string text, uint32_t serial);
    // The selection's text as last read (empty: none, or not text).
    [[nodiscard]] const std::string& text(Selection which) const noexcept {
        return slots_.at(static_cast<std::size_t>(which)).text;
    }
    [[nodiscard]] bool has_primary() const noexcept { return primary_device_ != nullptr; }

    // Called when a selection's text changed (read from another client).
    std::function<void(Selection)> on_changed;

private:
    struct Offer {
        void* proxy = nullptr; // wl_data_offer or zwp_primary_selection_offer_v1
        Selection kind = Selection::Clipboard;
        std::vector<std::string> mime_types;
    };
    struct Slot {
        std::string text;
        void* offer = nullptr;  // the current selection's offer (another client's)
        void* source = nullptr; // our own, while we hold the selection
        int reading_fd = -1;
        std::string incoming;
    };

    friend struct ClipboardListeners;
    Offer* find_offer(void* proxy);
    static void destroy_offer(const Offer& offer);
    void take_selection(Selection which, void* offer);
    void read(Selection which, const Offer& offer);
    void finish_read(Selection which, bool complete);
    void destroy_source(Selection which);
    void send(Selection which, const char* mime_type, int fd) const;

    App& app_;
    wl_data_device_manager* data_manager_;
    wl_data_device* data_device_ = nullptr;
    zwp_primary_selection_device_manager_v1* primary_manager_;
    zwp_primary_selection_device_v1* primary_device_ = nullptr;
    std::vector<Offer> offers_; // announced, not yet a selection or destroyed
    std::array<Slot, 2> slots_;
};

} // namespace tetra::ui
