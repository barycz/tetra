#pragma once

#include "app.hpp"
#include "protocols.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tetra::settings {

struct Display;
class Displays;

struct DisplayMode {
    zwlr_output_mode_v1* proxy = nullptr;
    Display* owner = nullptr;
    int width = 0;
    int height = 0;
    int refresh = 0; // mHz; 0 if unknown
    bool preferred = false;
};

// A display as the compositor reports it (wlr-output-management).
struct Display {
    zwlr_output_head_v1* proxy = nullptr;
    Displays* list = nullptr;
    std::string name; // connector, e.g. "DP-1": the key in config.json
    std::string description;
    std::string make;
    std::string model;
    int physical_width = 0; // mm
    int physical_height = 0;
    std::vector<std::unique_ptr<DisplayMode>> modes;
    DisplayMode* current = nullptr;
    bool enabled = false;
    int x = 0;
    int y = 0;
    double scale = 1;

    // Size in the layout: the mode's pixels divided by the scale.
    [[nodiscard]] int logical_width() const;
    [[nodiscard]] int logical_height() const;
    // Headless and nested outputs take any size; they report one mode.
    [[nodiscard]] bool any_size() const;
    // "Dell Inc. U2720Q", or the connector's description.
    [[nodiscard]] std::string title() const;
};

// The compositor's displays, kept up to date. Read-only: changes go
// through config.json like every other setting.
class Displays {
public:
    explicit Displays(ui::App& app);
    ~Displays();

    Displays(const Displays&) = delete;
    Displays& operator=(const Displays&) = delete;
    Displays(Displays&&) = delete;
    Displays& operator=(Displays&&) = delete;

    [[nodiscard]] bool available() const noexcept { return manager_ != nullptr; }
    [[nodiscard]] const std::vector<std::unique_ptr<Display>>& list() const noexcept { return displays_; }

    // After each complete update from the compositor.
    std::function<void()> on_changed;

private:
    friend struct DisplayListeners;
    void remove_mode(DisplayMode* mode);
    void remove_display(Display* display);

    zwlr_output_manager_v1* manager_ = nullptr;
    std::vector<std::unique_ptr<Display>> displays_;
};

} // namespace tetra::settings
