#pragma once

#include "config.hpp"

namespace tetra::theme {

using config::Color;

// Concrete colours every component draws with, derived from the theme
// settings. Keeping the derivation in one place is what keeps the compositor's
// decorations, the shell and the apps visually consistent.
struct Palette {
    Color window;         // app window background
    Color surface;        // raised surfaces: bar, cards, menus
    Color hover;          // hover overlay on surfaces
    Color pressed;        // pressed overlay on surfaces
    Color text;
    Color text_dim;
    Color accent;
    Color accent_text;    // text drawn on the accent colour
    Color separator;

    Color titlebar_active;
    Color titlebar_inactive;
    Color title_active;
    Color title_inactive;
    Color border_active;
    Color border_inactive;
    Color close_hover;    // close button background on hover
};

Palette make_palette(const config::Theme& theme);

} // namespace tetra::theme
