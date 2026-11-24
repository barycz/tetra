#include "config.hpp"
#include "zones.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace tetra::config {

namespace {

using json = nlohmann::json;

// Thrown for type errors; the message carries the JSON path.
struct ConfigError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

std::optional<int> hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return std::nullopt;
}

class Reader {
public:
    explicit Reader(std::vector<std::string>* warnings) : warnings_(warnings) {}

    // Reports keys of `object` (at `path`) that are not in `known`.
    void check_keys(const json& object, std::string_view path,
                    std::initializer_list<std::string_view> known) {
        if (warnings_ == nullptr) {
            return;
        }
        for (const auto& [key, value] : object.items()) {
            bool found = false;
            for (std::string_view k : known) {
                found = found || k == key;
            }
            if (!found) {
                warnings_->push_back(std::format("unknown key '{}{}'", path, key));
            }
        }
    }

    static const json& expect_object(const json& value, std::string_view path) {
        if (!value.is_object()) {
            throw ConfigError(std::format("'{}' must be an object", path));
        }
        return value;
    }

    static void read(const json& object, std::string_view path, const char* key, bool& out) {
        if (const auto* v = find(object, key)) {
            if (!v->is_boolean()) {
                throw ConfigError(std::format("'{}{}' must be true or false", path, key));
            }
            out = v->get<bool>();
        }
    }

    static void read(const json& object, std::string_view path, const char* key, int& out) {
        if (const auto* v = find(object, key)) {
            if (!v->is_number_integer()) {
                throw ConfigError(std::format("'{}{}' must be an integer", path, key));
            }
            out = v->get<int>();
        }
    }

    static void read(const json& object, std::string_view path, const char* key, double& out) {
        if (const auto* v = find(object, key)) {
            if (!v->is_number()) {
                throw ConfigError(std::format("'{}{}' must be a number", path, key));
            }
            out = v->get<double>();
        }
    }

    static void read(const json& object, std::string_view path, const char* key,
                     std::string& out) {
        if (const auto* v = find(object, key)) {
            if (!v->is_string()) {
                throw ConfigError(std::format("'{}{}' must be a string", path, key));
            }
            out = v->get<std::string>();
        }
    }

    static void read(const json& object, std::string_view path, const char* key, Color& out) {
        std::string text;
        read(object, path, key, text);
        if (text.empty()) {
            return;
        }
        auto color = Color::parse(text);
        if (!color) {
            throw ConfigError(
                std::format("'{}{}' must be a colour like \"#rrggbb\", got \"{}\"", path, key, text));
        }
        out = *color;
    }

    static void read(const json& object, std::string_view path, const char* key,
                     std::vector<std::string>& out) {
        if (const auto* v = find(object, key)) {
            if (!v->is_array()) {
                throw ConfigError(std::format("'{}{}' must be an array of strings", path, key));
            }
            out.clear();
            for (const auto& item : *v) {
                if (!item.is_string()) {
                    throw ConfigError(std::format("'{}{}' must be an array of strings", path, key));
                }
                out.push_back(item.get<std::string>());
            }
        }
    }

private:
    static const json* find(const json& object, const char* key) {
        auto it = object.find(key);
        return it == object.end() || it->is_null() ? nullptr : &*it;
    }

    std::vector<std::string>* warnings_;
};

// "1920x1080" or "1920x1080@60".
OutputMode parse_mode(std::string_view text, std::string_view path) {
    OutputMode mode;
    const auto x = text.find('x');
    const auto at = text.find('@');
    auto fail = [&] {
        return ConfigError(
            std::format("'{}' must look like \"1920x1080\" or \"1920x1080@60\", got \"{}\"", path,
                        text));
    };
    if (x == std::string_view::npos) {
        throw fail();
    }
    const auto end = at == std::string_view::npos ? text.size() : at;
    const auto w = std::from_chars(text.data(), text.data() + x, mode.width);
    const auto h = std::from_chars(text.data() + x + 1, text.data() + end, mode.height);
    if (w.ec != std::errc{} || h.ec != std::errc{} || mode.width <= 0 || mode.height <= 0) {
        throw fail();
    }
    if (at != std::string_view::npos) {
        const auto r = std::from_chars(text.data() + at + 1, text.data() + text.size(), mode.refresh);
        if (r.ec != std::errc{} || mode.refresh <= 0) {
            throw fail();
        }
    }
    return mode;
}

Config parse(const json& root, Reader& reader) {
    Config config;
    Reader::expect_object(root, "");
    reader.check_keys(root, "",
                      {"$schema", "keyboard", "mouse", "touchpad", "outputs", "theme", "desktop",
                       "windows", "taskbar", "idle", "shell", "terminal", "autostart"});

    if (auto it = root.find("keyboard"); it != root.end()) {
        const auto& kb = Reader::expect_object(*it, "keyboard");
        reader.check_keys(kb, "keyboard.",
                          {"layout", "model", "variant", "options", "repeat_rate", "repeat_delay"});
        Reader::read(kb, "keyboard.", "layout", config.keyboard.layout);
        Reader::read(kb, "keyboard.", "model", config.keyboard.model);
        Reader::read(kb, "keyboard.", "variant", config.keyboard.variant);
        Reader::read(kb, "keyboard.", "options", config.keyboard.options);
        Reader::read(kb, "keyboard.", "repeat_rate", config.keyboard.repeat_rate);
        Reader::read(kb, "keyboard.", "repeat_delay", config.keyboard.repeat_delay);
    }

    auto read_speed = [](const json& object, const char* path, double& out) {
        Reader::read(object, path, "speed", out);
        if (out < -1 || out > 1) {
            throw ConfigError(std::format("'{}speed' must be between -1 (slowest) and 1 (fastest)", path));
        }
    };

    if (auto it = root.find("mouse"); it != root.end()) {
        const auto& mouse = Reader::expect_object(*it, "mouse");
        reader.check_keys(mouse, "mouse.", {"speed", "acceleration", "natural_scroll", "left_handed"});
        read_speed(mouse, "mouse.", config.mouse.speed);
        Reader::read(mouse, "mouse.", "acceleration", config.mouse.acceleration);
        Reader::read(mouse, "mouse.", "natural_scroll", config.mouse.natural_scroll);
        Reader::read(mouse, "mouse.", "left_handed", config.mouse.left_handed);
    }

    if (auto it = root.find("touchpad"); it != root.end()) {
        const auto& touchpad = Reader::expect_object(*it, "touchpad");
        reader.check_keys(touchpad, "touchpad.",
                          {"speed", "tap_to_click", "natural_scroll", "disable_while_typing", "scroll",
                           "click"});
        auto& tp = config.touchpad;
        read_speed(touchpad, "touchpad.", tp.speed);
        Reader::read(touchpad, "touchpad.", "tap_to_click", tp.tap_to_click);
        Reader::read(touchpad, "touchpad.", "natural_scroll", tp.natural_scroll);
        Reader::read(touchpad, "touchpad.", "disable_while_typing", tp.disable_while_typing);
        std::string scroll;
        Reader::read(touchpad, "touchpad.", "scroll", scroll);
        if (scroll == "two_finger") {
            tp.scroll = Touchpad::Scroll::TwoFinger;
        } else if (scroll == "edge") {
            tp.scroll = Touchpad::Scroll::Edge;
        } else if (scroll == "none") {
            tp.scroll = Touchpad::Scroll::None;
        } else if (!scroll.empty()) {
            throw ConfigError(std::format(
                "'touchpad.scroll' must be \"two_finger\", \"edge\" or \"none\", got \"{}\"", scroll));
        }
        std::string click;
        Reader::read(touchpad, "touchpad.", "click", click);
        if (click == "areas") {
            tp.click = Touchpad::Click::Areas;
        } else if (click == "fingers") {
            tp.click = Touchpad::Click::Fingers;
        } else if (!click.empty()) {
            throw ConfigError(
                std::format("'touchpad.click' must be \"areas\" or \"fingers\", got \"{}\"", click));
        }
    }

    if (auto it = root.find("outputs"); it != root.end()) {
        if (!it->is_array()) {
            throw ConfigError("'outputs' must be an array");
        }
        for (std::size_t i = 0; i < it->size(); ++i) {
            const std::string path = std::format("outputs[{}].", i);
            const auto& entry = Reader::expect_object((*it)[i], path);
            reader.check_keys(entry, path, {"name", "enabled", "mode", "scale", "position", "zones"});
            Output output;
            Reader::read(entry, path, "name", output.name);
            if (output.name.empty()) {
                throw ConfigError(std::format("'{}name' is required", path));
            }
            Reader::read(entry, path, "enabled", output.enabled);
            Reader::read(entry, path, "scale", output.scale);
            if (output.scale < 0.5 || output.scale > 4) {
                throw ConfigError(std::format("'{}scale' must be between 0.5 and 4", path));
            }
            std::string mode;
            Reader::read(entry, path, "mode", mode);
            if (!mode.empty()) {
                output.mode = parse_mode(mode, path + "mode");
            }
            if (auto pos = entry.find("position"); pos != entry.end()) {
                if (!pos->is_array() || pos->size() != 2 || !(*pos)[0].is_number_integer() ||
                    !(*pos)[1].is_number_integer()) {
                    throw ConfigError(std::format("'{}position' must be [x, y]", path));
                }
                output.position = std::pair{(*pos)[0].get<int>(), (*pos)[1].get<int>()};
            }
            std::string zones;
            Reader::read(entry, path, "zones", zones);
            if (!zones.empty()) {
                output.zones = zones;
            }
            config.outputs.push_back(std::move(output));
        }
    }

    if (auto it = root.find("theme"); it != root.end()) {
        const auto& theme = Reader::expect_object(*it, "theme");
        reader.check_keys(theme, "theme.", {"dark", "accent", "font", "font_size"});
        Reader::read(theme, "theme.", "dark", config.theme.dark);
        Reader::read(theme, "theme.", "accent", config.theme.accent);
        Reader::read(theme, "theme.", "font", config.theme.font);
        Reader::read(theme, "theme.", "font_size", config.theme.font_size);
        if (config.theme.font_size < 6 || config.theme.font_size > 48) {
            throw ConfigError("'theme.font_size' must be between 6 and 48");
        }
    }

    if (auto it = root.find("desktop"); it != root.end()) {
        const auto& desktop = Reader::expect_object(*it, "desktop");
        reader.check_keys(desktop, "desktop.", {"background"});
        Reader::read(desktop, "desktop.", "background", config.desktop.background);
    }

    if (auto it = root.find("windows"); it != root.end()) {
        const auto& windows = Reader::expect_object(*it, "windows");
        reader.check_keys(windows, "windows.",
                          {"titlebar_height", "border_width", "remember_positions", "zones", "zone_gap",
                           "zone_layouts"});
        Reader::read(windows, "windows.", "titlebar_height", config.windows.titlebar_height);
        Reader::read(windows, "windows.", "border_width", config.windows.border_width);
        Reader::read(windows, "windows.", "remember_positions", config.windows.remember_positions);
        Reader::read(windows, "windows.", "zones", config.windows.zones);
        Reader::read(windows, "windows.", "zone_gap", config.windows.zone_gap);
        if (config.windows.zone_gap < 0 || config.windows.zone_gap > 64) {
            throw ConfigError("'windows.zone_gap' must be between 0 and 64");
        }
        if (auto layouts = windows.find("zone_layouts"); layouts != windows.end() && !layouts->is_null()) {
            if (!layouts->is_object()) {
                throw ConfigError("'windows.zone_layouts' must be an object of named layouts");
            }
            for (const auto& [name, list] : layouts->items()) {
                const std::string path = "windows.zone_layouts." + name;
                const bool clash = std::ranges::any_of(zones::builtin(), [&](const auto& l) { return l.name == name; });
                if (clash) {
                    throw ConfigError(std::format("'{}': \"{}\" is a built-in layout; pick another name", path, name));
                }
                if (!list.is_array() || list.empty() || list.size() > 16) {
                    throw ConfigError(std::format("'{}' must be a list of 1 to 16 zones", path));
                }
                ZoneLayout layout{.name = name, .zones = {}};
                for (const auto& zone : list) {
                    const bool numbers = zone.is_array() && zone.size() == 4 &&
                                         std::ranges::all_of(zone, [](const json& v) { return v.is_number(); });
                    if (!numbers) {
                        throw ConfigError(std::format("'{}': each zone is [x, y, width, height] in percent", path));
                    }
                    const std::array<double, 4> z{zone[0].get<double>(), zone[1].get<double>(),
                                                  zone[2].get<double>(), zone[3].get<double>()};
                    if (z[0] < 0 || z[1] < 0 || z[2] <= 0 || z[3] <= 0 || z[0] + z[2] > 100.001 ||
                        z[1] + z[3] > 100.001) {
                        throw ConfigError(std::format("'{}': zones must lie within 0..100 percent", path));
                    }
                    layout.zones.push_back(z);
                }
                config.windows.zone_layouts.push_back(std::move(layout));
            }
        }
        if (config.windows.titlebar_height < 16 || config.windows.titlebar_height > 96) {
            throw ConfigError("'windows.titlebar_height' must be between 16 and 96");
        }
        if (config.windows.border_width < 0 || config.windows.border_width > 16) {
            throw ConfigError("'windows.border_width' must be between 0 and 16");
        }
    }

    if (auto it = root.find("taskbar"); it != root.end()) {
        const auto& taskbar = Reader::expect_object(*it, "taskbar");
        reader.check_keys(taskbar, "taskbar.", {"pinned"});
        Reader::read(taskbar, "taskbar.", "pinned", config.taskbar.pinned);
        if (config.taskbar.pinned.size() > Taskbar::kMaxPins) {
            throw ConfigError(std::format("'taskbar.pinned' holds at most {} apps (one slot stays "
                                          "free for other windows)",
                                          Taskbar::kMaxPins));
        }
    }

    if (auto it = root.find("idle"); it != root.end()) {
        const auto& idle = Reader::expect_object(*it, "idle");
        reader.check_keys(idle, "idle.", {"lock_after", "screen_off_after", "lock_before_sleep"});
        Reader::read(idle, "idle.", "lock_after", config.idle.lock_after);
        Reader::read(idle, "idle.", "screen_off_after", config.idle.screen_off_after);
        Reader::read(idle, "idle.", "lock_before_sleep", config.idle.lock_before_sleep);
        constexpr int kDay = 24 * 60 * 60;
        for (const auto& [key, value] : {std::pair{"lock_after", config.idle.lock_after},
                                         std::pair{"screen_off_after", config.idle.screen_off_after}}) {
            if (value < 0 || value > kDay) {
                throw ConfigError(std::format("'idle.{}' must be between 0 (never) and {} seconds",
                                              key, kDay));
            }
        }
    }

    auto known_layout = [&](const std::string& name) {
        return std::ranges::any_of(zones::builtin(), [&](const auto& l) { return l.name == name; }) ||
               std::ranges::any_of(config.windows.zone_layouts, [&](const auto& l) { return l.name == name; });
    };
    if (!known_layout(config.windows.zones)) {
        throw ConfigError(std::format("'windows.zones': no layout called \"{}\"", config.windows.zones));
    }
    for (const auto& output : config.outputs) {
        if (output.zones && !known_layout(*output.zones)) {
            throw ConfigError(std::format("'zones' of output \"{}\": no layout called \"{}\"", output.name,
                                          *output.zones));
        }
    }

    Reader::read(root, "", "shell", config.shell);
    Reader::read(root, "", "terminal", config.terminal);
    Reader::read(root, "", "autostart", config.autostart);
    return config;
}

} // namespace

std::string Color::hex() const {
    auto byte = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255)); };
    if (byte(a) == 255) {
        return std::format("#{:02x}{:02x}{:02x}", byte(r), byte(g), byte(b));
    }
    return std::format("#{:02x}{:02x}{:02x}{:02x}", byte(r), byte(g), byte(b), byte(a));
}

std::optional<Color> Color::parse(std::string_view text) {
    if (text.empty() || text.front() != '#') {
        return std::nullopt;
    }
    text.remove_prefix(1);
    std::array<int, 8> digits{};
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i >= digits.size()) {
            return std::nullopt;
        }
        auto d = hex_digit(text[i]);
        if (!d) {
            return std::nullopt;
        }
        digits.at(i) = *d;
    }
    auto byte = [&](std::size_t i) { return static_cast<float>(digits.at(i) * 16 + digits.at(i + 1)) / 255.0F; };
    auto nibble = [&](std::size_t i) { return static_cast<float>(digits.at(i) * 17) / 255.0F; };
    switch (text.size()) {
    case 3:
        return Color{nibble(0), nibble(1), nibble(2), 1.0F};
    case 6:
        return Color{byte(0), byte(2), byte(4), 1.0F};
    case 8:
        return Color{byte(0), byte(2), byte(4), byte(6)};
    default:
        return std::nullopt;
    }
}

const Output* Config::output(std::string_view name) const {
    const Output* wildcard = nullptr;
    for (const auto& output : outputs) {
        if (output.name == name) {
            return &output;
        }
        if (output.name == "*") {
            wildcard = &output;
        }
    }
    return wildcard;
}

std::filesystem::path config_dir() {
    // NOLINTBEGIN(concurrency-mt-unsafe)
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg == '/') {
        return std::filesystem::path(xdg) / "tetra";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".config" / "tetra";
    }
    // NOLINTEND(concurrency-mt-unsafe)
    return std::filesystem::path("/etc/xdg/tetra");
}

std::filesystem::path config_file() {
    return config_dir() / "config.json";
}

std::filesystem::path active_file() {
    if (const char* env = std::getenv("TETRA_CONFIG"); env != nullptr && *env != '\0') { // NOLINT
        return env;
    }
    return config_file();
}

std::expected<Config, std::string> load(const std::filesystem::path& path,
                                        std::vector<std::string>* warnings) {
    std::ifstream stream(path);
    if (!stream) {
        if (!std::filesystem::exists(path)) {
            return Config{};
        }
        return std::unexpected(std::format("{}: cannot be read", path.string()));
    }
    std::stringstream buffer;
    buffer << stream.rdbuf();
    auto config = parse_text(buffer.str(), warnings);
    if (!config) {
        return std::unexpected(std::format("{}: {}", path.string(), config.error()));
    }
    return config;
}

std::expected<Config, std::string> parse_text(std::string_view text,
                                              std::vector<std::string>* warnings) {
    try {
        const json root = json::parse(text, nullptr, /*allow_exceptions=*/true,
                                      /*ignore_comments=*/true);
        Reader reader(warnings);
        return parse(root, reader);
    } catch (const json::parse_error& e) {
        return std::unexpected(e.what());
    } catch (const ConfigError& e) {
        return std::unexpected(e.what());
    }
}

} // namespace tetra::config
