#pragma once

#include "config.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace tetra::portal {

// What the Settings portal publishes, derived from config.json's theme:
//   org.freedesktop.appearance  color-scheme (u: 1 dark, 2 light),
//                               accent-color ((ddd), 0..1), contrast (u: 0)
//   org.gnome.desktop.interface color-scheme ("prefer-dark"/"prefer-light"),
//                               gtk-theme ("Adwaita-dark"/"Adwaita"),
//                               accent-color (GNOME's nearest named colour),
//                               font-name ("Inter 10.5": points),
//                               enable-animations (theme.animations)
// GTK 4 and libadwaita read the first namespace; GTK 3 and others the second.
struct Appearance {
    uint32_t color_scheme = 0;
    std::array<double, 3> accent{};
    std::string gnome_color_scheme;
    std::string gtk_theme;
    std::string gnome_accent;
    std::string font_name;
    bool animations = true;
    bool operator==(const Appearance&) const = default;
};

[[nodiscard]] Appearance appearance(const config::Theme& theme);

// GNOME's accent colour names (blue, teal, ...): the one nearest `color`'s
// hue, or "slate" for greys.
[[nodiscard]] std::string nearest_gnome_accent(const config::Color& color);

} // namespace tetra::portal
