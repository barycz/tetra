#include "appearance.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace tetra::portal {

std::string nearest_gnome_accent(const config::Color& color) {
    // GNOME 47's accent colours by hue (degrees); greys are "slate".
    struct Named {
        const char* name;
        float hue;
    };
    static constexpr std::array<Named, 8> kHues = {{
        {"blue", 213},
        {"teal", 190},
        {"green", 129},
        {"yellow", 41},
        {"orange", 23},
        {"red", 353},
        {"pink", 331},
        {"purple", 285},
    }};
    const float max = std::max({color.r, color.g, color.b});
    const float min = std::min({color.r, color.g, color.b});
    const float chroma = max - min;
    if (max <= 0 || chroma / max < 0.25F) {
        return "slate";
    }
    float hue = 0;
    if (max == color.r) {
        hue = 60 * std::fmod(((color.g - color.b) / chroma) + 6, 6.0F);
    } else if (max == color.g) {
        hue = 60 * (((color.b - color.r) / chroma) + 2);
    } else {
        hue = 60 * (((color.r - color.g) / chroma) + 4);
    }
    const char* best = "blue";
    float best_distance = std::numeric_limits<float>::max();
    for (const auto& named : kHues) {
        const float d = std::abs(hue - named.hue);
        const float distance = std::min(d, 360 - d); // around the circle
        if (distance < best_distance) {
            best_distance = distance;
            best = named.name;
        }
    }
    return best;
}

Appearance appearance(const config::Theme& theme) {
    Appearance a;
    a.color_scheme = theme.dark ? 1 : 2;
    a.accent = {theme.accent.r, theme.accent.g, theme.accent.b};
    a.gnome_color_scheme = theme.dark ? "prefer-dark" : "prefer-light";
    a.gtk_theme = theme.dark ? "Adwaita-dark" : "Adwaita";
    a.gnome_accent = nearest_gnome_accent(theme.accent);
    // Pixels at 96 dpi -> points, to the nearest half point.
    const double points = std::round(theme.font_size * 0.75 * 2) / 2;
    a.font_name = std::format("{} {}", theme.font, points);
    return a;
}

} // namespace tetra::portal
