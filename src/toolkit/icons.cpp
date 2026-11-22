#include "icons.hpp"

#include "app.hpp"

#include <GLES3/gl3.h>
#include <cairo.h>
#include <librsvg/rsvg.h>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <span>

namespace tetra::ui {

namespace {

namespace fs = std::filesystem;

std::vector<fs::path> icon_base_dirs() {
    std::vector<fs::path> dirs;
    // NOLINTBEGIN(concurrency-mt-unsafe)
    const char* home = std::getenv("HOME");
    if (const char* data_home = std::getenv("XDG_DATA_HOME"); data_home != nullptr && *data_home == '/') {
        dirs.emplace_back(fs::path(data_home) / "icons");
    } else if (home != nullptr) {
        dirs.emplace_back(fs::path(home) / ".local/share/icons");
    }
    if (home != nullptr) {
        dirs.emplace_back(fs::path(home) / ".icons");
    }
    const char* data_dirs = std::getenv("XDG_DATA_DIRS");
    // NOLINTEND(concurrency-mt-unsafe)
    std::string list = data_dirs != nullptr && *data_dirs != '\0' ? data_dirs : "/usr/local/share:/usr/share";
    std::size_t start = 0;
    while (start <= list.size()) {
        const auto end = list.find(':', start);
        const auto part = list.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty()) {
            dirs.emplace_back(fs::path(part) / "icons");
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return dirs;
}

// "48x48" -> 48, "scalable"/"symbolic" -> 0, anything else -> nullopt.
std::optional<int> dir_size(const std::string& dir) {
    if (dir == "scalable" || dir == "symbolic") {
        return 0;
    }
    int size = 0;
    const auto x = dir.find('x');
    if (x == std::string::npos || dir.find('@') != std::string::npos ||
        std::from_chars(dir.data(), dir.data() + x, size).ec != std::errc{}) {
        return std::nullopt;
    }
    return size;
}

// Renders `path` into a size x size ARGB32 (premultiplied) cairo surface.
cairo_surface_t* render(const fs::path& path, int size) {
    cairo_surface_t* target = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t* cr = cairo_create(target);
    bool ok = false;
    if (path.extension() == ".svg") {
        GError* error = nullptr;
        RsvgHandle* handle = rsvg_handle_new_from_file(path.c_str(), &error);
        if (handle != nullptr) {
            const RsvgRectangle viewport{0, 0, static_cast<double>(size), static_cast<double>(size)};
            ok = rsvg_handle_render_document(handle, cr, &viewport, &error) != FALSE;
            g_object_unref(handle);
        }
        if (error != nullptr) {
            g_error_free(error);
        }
    } else if (path.extension() == ".png") {
        cairo_surface_t* png = cairo_image_surface_create_from_png(path.c_str());
        if (cairo_surface_status(png) == CAIRO_STATUS_SUCCESS) {
            const double sx = static_cast<double>(size) / cairo_image_surface_get_width(png);
            const double sy = static_cast<double>(size) / cairo_image_surface_get_height(png);
            cairo_scale(cr, sx, sy);
            cairo_set_source_surface(cr, png, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
            cairo_paint(cr);
            ok = true;
        }
        cairo_surface_destroy(png);
    }
    cairo_destroy(cr);
    if (!ok) {
        cairo_surface_destroy(target);
        return nullptr;
    }
    cairo_surface_flush(target);
    return target;
}

// Premultiplied BGRA (cairo, little endian) -> straight RGBA (ImGui blending).
// Symbolic icons become white with their alpha kept, for tinting.
std::vector<unsigned char> to_rgba(cairo_surface_t* surface, bool symbolic) {
    const int w = cairo_image_surface_get_width(surface);
    const int h = cairo_image_surface_get_height(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    const unsigned char* data = cairo_image_surface_get_data(surface);
    std::vector<unsigned char> out(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        const std::span row(data + (static_cast<std::ptrdiff_t>(y) * stride), static_cast<std::size_t>(w) * 4);
        for (int x = 0; x < w; ++x) {
            const auto i = static_cast<std::size_t>(x) * 4;
            const unsigned a = row[i + 3];
            auto unpremultiply = [&](unsigned c) {
                return static_cast<unsigned char>(a == 0 ? 0 : std::min(255U, (c * 255 + a / 2) / a));
            };
            const auto o = (static_cast<std::size_t>(y) * w + x) * 4;
            out[o + 0] = symbolic ? 255 : unpremultiply(row[i + 2]);
            out[o + 1] = symbolic ? 255 : unpremultiply(row[i + 1]);
            out[o + 2] = symbolic ? 255 : unpremultiply(row[i + 0]);
            out[o + 3] = static_cast<unsigned char>(a);
        }
    }
    return out;
}

} // namespace

IconCache::IconCache(App& app)
    : app_(app), base_dirs_(icon_base_dirs()), themes_{"Adwaita", "hicolor"} {}

IconCache::~IconCache() {
    app_.make_gl_current();
    for (const auto& [key, icon] : icons_) {
        if (icon) {
            glDeleteTextures(1, &icon->texture);
        }
    }
}

const IconCache::ThemeIndex& IconCache::theme_index(const std::string& theme) {
    if (auto it = indexes_.find(theme); it != indexes_.end()) {
        return it->second;
    }
    // One walk over the theme's directories, then lookups are in memory.
    ThemeIndex index;
    for (const auto& base : base_dirs_) {
        std::error_code ec;
        const fs::path root = base / theme;
        for (auto it = fs::recursive_directory_iterator(root, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            const auto& path = it->path();
            const auto ext = path.extension();
            if (ext != ".png" && ext != ".svg") {
                continue;
            }
            const auto rel = path.lexically_relative(root);
            if (rel.begin() == rel.end()) {
                continue;
            }
            if (const auto size = dir_size(rel.begin()->string())) {
                index[path.stem().string()].push_back({path, *size});
            }
        }
    }
    return indexes_.emplace(theme, std::move(index)).first->second;
}

std::optional<fs::path> IconCache::lookup(const std::string& name, int size) {
    if (!name.empty() && name.front() == '/') {
        return fs::exists(name) ? std::optional<fs::path>(name) : std::nullopt;
    }
    for (const auto& theme : themes_) {
        const auto& index = theme_index(theme);
        auto it = index.find(name);
        if (it == index.end()) {
            continue;
        }
        // Prefer: scalable SVG, then the smallest bitmap >= size, then the largest.
        const Candidate* best = nullptr;
        auto score = [&](const Candidate& c) {
            if (c.size == 0) {
                return 0;
            }
            return c.size >= size ? c.size - size + 1 : 100000 - c.size;
        };
        for (const auto& candidate : it->second) {
            if (best == nullptr || score(candidate) < score(*best)) {
                best = &candidate;
            }
        }
        if (best != nullptr) {
            return best->path;
        }
    }
    for (const auto& base : {fs::path("/usr/share/pixmaps")}) {
        for (const char* ext : {".svg", ".png"}) {
            const auto path = base / (name + ext);
            if (fs::exists(path)) {
                return path;
            }
        }
    }
    return std::nullopt;
}

const Icon* IconCache::get(const std::string& name, int size) {
    if (name.empty() || size <= 0) {
        return nullptr;
    }
    const std::string key = name + "@" + std::to_string(size);
    if (auto it = icons_.find(key); it != icons_.end()) {
        return it->second ? &*it->second : nullptr;
    }

    std::optional<Icon> icon;
    if (const auto path = lookup(name, size)) {
        if (cairo_surface_t* surface = render(*path, size)) {
            const bool symbolic = name.ends_with("-symbolic");
            const auto pixels = to_rgba(surface, symbolic);
            cairo_surface_destroy(surface);

            GLuint texture = 0;
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         pixels.data());
            icon = Icon{.texture = texture, .size = size, .symbolic = symbolic};
        }
    }
    auto [it, inserted] = icons_.emplace(key, icon);
    return it->second ? &*it->second : nullptr;
}

} // namespace tetra::ui
