#pragma once

#include <array>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// tetra's single configuration tree (~/.config/tetra/config.json).
// Every component reads the same file; missing keys take the defaults below.
namespace tetra::config {

struct Color {
    float r = 0;
    float g = 0;
    float b = 0;
    float a = 1;

    // "#rgb", "#rrggbb" or "#rrggbbaa".
    static std::optional<Color> parse(std::string_view text);
    // "#rrggbb", or "#rrggbbaa" when not opaque.
    [[nodiscard]] std::string hex() const;
    [[nodiscard]] std::array<float, 4> rgba() const { return {r, g, b, a}; }
    // Premultiplied, as wlroots scene rects expect.
    [[nodiscard]] std::array<float, 4> premultiplied() const { return {r * a, g * a, b * a, a}; }
    [[nodiscard]] Color with_alpha(float alpha) const { return {r, g, b, alpha}; }
    bool operator==(const Color&) const = default;
};

struct Keyboard {
    std::string layout = "us";
    std::string model;
    std::string variant;
    std::string options;
    int repeat_rate = 25;   // keys per second
    int repeat_delay = 600; // milliseconds
    bool operator==(const Keyboard&) const = default;
};

// Mice (and trackpoints, trackballs). left_handed also applies to touchpads.
struct Mouse {
    double speed = 0;          // -1 (slowest) to 1 (fastest)
    bool acceleration = true;  // false: the pointer moves as far as the mouse, however fast
    bool natural_scroll = false;
    bool left_handed = false;  // swaps the primary and secondary buttons
    bool operator==(const Mouse&) const = default;
};

struct Touchpad {
    enum class Scroll { TwoFinger, Edge, None };
    enum class Click { Areas, Fingers };
    double speed = 0;
    bool tap_to_click = true;
    bool natural_scroll = true; // content follows the fingers
    bool disable_while_typing = true;
    Scroll scroll = Scroll::TwoFinger;
    // Areas: the bottom right of the pad is a right click. Fingers: a click
    // with two fingers is a right click, three a middle click. None: the
    // touchpad's own default.
    std::optional<Click> click;
    bool operator==(const Touchpad&) const = default;
};

struct OutputMode {
    int width = 0;
    int height = 0;
    double refresh = 0; // Hz; 0 picks the highest available
    bool operator==(const OutputMode&) const = default;
};

struct Output {
    std::string name; // connector name, e.g. "DP-1", "Virtual-1", or "*" for all
    bool enabled = true;
    std::optional<OutputMode> mode;
    double scale = 1.0;
    std::optional<std::pair<int, int>> position;
    std::optional<std::string> zones; // snap zone layout; none: windows.zones
    bool operator==(const Output&) const = default;
};

struct Theme {
    bool dark = true;
    Color accent = *Color::parse("#4c8dff");
    std::string font = "Inter";
    double font_size = 14; // pixels at scale 1
    bool animations = true; // windows fade and move, panels slide
    bool operator==(const Theme&) const = default;
};

// What the desktop shows behind the windows.
struct Desktop {
    // The tetra mark drawn in the accent colour, a picture, or the colour alone.
    enum class Wallpaper { Logo, Picture, Color };
    // How a picture covers the display: all of it, cropping the picture
    // (fill); the whole picture, with the colour around it (fit); or the
    // picture at its own size in the middle (center).
    enum class Fit { Fill, Fit, Center };

    Wallpaper wallpaper = Wallpaper::Logo;
    Color background = *Color::parse("#1b1d22"); // also behind the logo and around a picture
    std::string picture;                         // absolute, "~/" expanded
    Fit picture_fit = Fit::Fill;
    bool operator==(const Desktop&) const = default;
};

// A snap zone layout of the user's own: zones as [x, y, width, height] in
// percent of the display.
struct ZoneLayout {
    std::string name;
    std::vector<std::array<double, 4>> zones;
    bool operator==(const ZoneLayout&) const = default;
};

struct Windows {
    int titlebar_height = 32;
    int border_width = 1;
    // Windows open where they were last (~/.local/state/tetra/windows.json).
    bool remember_positions = true;
    // Snap zones (Shift while dragging): the layout for displays that do not
    // name one, the gap between zones, and layouts of the user's own.
    std::string zones = "columns-2";
    int zone_gap = 8;
    std::vector<ZoneLayout> zone_layouts;
    bool operator==(const Windows&) const = default;
};

struct Taskbar {
    static constexpr std::size_t kMaxPins = 11; // one slot always stays free for overflow
    // Desktop file IDs or app_ids, in slot order (Super+F1, Super+F2, ...).
    std::vector<std::string> pinned;
    bool operator==(const Taskbar&) const = default;
};

struct Idle {
    // Seconds without input; 0 disables. Apps playing video can inhibit both.
    int lock_after = 300;
    int screen_off_after = 600;
    bool lock_before_sleep = true; // suspend/hibernate lock first
    bool operator==(const Idle&) const = default;
};

struct Config {
    Keyboard keyboard;
    Mouse mouse;
    Touchpad touchpad;
    std::vector<Output> outputs;
    Theme theme;
    Desktop desktop;
    Windows windows;
    Taskbar taskbar;
    Idle idle;
    std::string shell = "tetra-shell"; // empty: do not start a shell
    std::string terminal = "tetra-terminal"; // for Terminal=true apps and Super+Return
    std::vector<std::string> autostart;

    // Settings for the named output: exact match first, then "*".
    [[nodiscard]] const Output* output(std::string_view name) const;
    bool operator==(const Config&) const = default;
};

// $XDG_CONFIG_HOME/tetra, falling back to ~/.config/tetra.
std::filesystem::path config_dir();
std::filesystem::path config_file();
// The file in use: $TETRA_CONFIG (set by `tetra -c`) or config_file().
std::filesystem::path active_file();

// A missing file yields the defaults. Unknown keys are reported in `warnings`
// but are not errors.
std::expected<Config, std::string> load(const std::filesystem::path& path,
                                        std::vector<std::string>* warnings = nullptr);
// The same for the file's text (errors without a file name).
std::expected<Config, std::string> parse_text(std::string_view text,
                                              std::vector<std::string>* warnings = nullptr);

} // namespace tetra::config
