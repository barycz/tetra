#pragma once

#include <imgui.h>

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tetra::ui {

class App;

struct Icon {
    unsigned int texture = 0; // GL texture name
    int size = 0;             // pixels
    // Symbolic icons are stored white; tint them when drawing.
    bool symbolic = false;

    [[nodiscard]] ImTextureRef ref() const { return ImTextureRef(static_cast<ImTextureID>(texture)); }
};

// freedesktop icon theme lookup (Adwaita, then hicolor, then pixmaps) with
// PNG and SVG loading into GL textures. Results, including misses, are cached.
// Use from inside a Window's draw callback (the GL context is current there).
class IconCache {
public:
    explicit IconCache(App& app);
    ~IconCache();

    IconCache(const IconCache&) = delete;
    IconCache& operator=(const IconCache&) = delete;
    IconCache(IconCache&&) = delete;
    IconCache& operator=(IconCache&&) = delete;

    // `name` is an icon name ("firefox-esr") or an absolute path; `size` in
    // pixels (already multiplied by the output scale). Null if not found.
    const Icon* get(const std::string& name, int size);

private:
    struct Candidate {
        std::filesystem::path path;
        int size; // 0 for scalable
    };
    using ThemeIndex = std::unordered_map<std::string, std::vector<Candidate>>;

    std::optional<std::filesystem::path> lookup(const std::string& name, int size);
    const ThemeIndex& theme_index(const std::string& theme);

    App& app_;
    std::vector<std::filesystem::path> base_dirs_;
    std::vector<std::string> themes_;
    std::unordered_map<std::string, ThemeIndex> indexes_;
    std::unordered_map<std::string, std::optional<Icon>> icons_; // key: name@size
};

} // namespace tetra::ui
