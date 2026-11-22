#include "theme.hpp"

namespace tetra::theme {

namespace {

Color hex(const char* text) {
    return *Color::parse(text);
}

// Perceived brightness, to pick readable text on the accent colour.
float luminance(const Color& c) {
    return (0.2126F * c.r) + (0.7152F * c.g) + (0.0722F * c.b);
}

} // namespace

Palette make_palette(const config::Theme& theme) {
    Palette p;
    p.accent = theme.accent;
    p.accent_text = luminance(theme.accent) > 0.55F ? hex("#101114") : hex("#ffffff");
    p.close_hover = hex("#c42b1c");

    if (theme.dark) {
        p.window = hex("#1e2025");
        p.surface = hex("#17191d");
        p.hover = hex("#ffffff14");
        p.pressed = hex("#ffffff0c");
        p.text = hex("#e8eaef");
        p.text_dim = hex("#8b8f98");
        p.separator = hex("#2c2f36");
        p.titlebar_active = hex("#26282e");
        p.titlebar_inactive = hex("#1e2025");
        p.title_active = hex("#e8eaef");
        p.title_inactive = hex("#7d818a");
        p.border_active = theme.accent.with_alpha(0.85F);
        p.border_inactive = hex("#34373e");
    } else {
        p.window = hex("#f4f5f7");
        p.surface = hex("#ebedf0");
        p.hover = hex("#0000000f");
        p.pressed = hex("#00000008");
        p.text = hex("#1b1d22");
        p.text_dim = hex("#646872");
        p.separator = hex("#d5d8de");
        p.titlebar_active = hex("#ffffff");
        p.titlebar_inactive = hex("#f4f5f7");
        p.title_active = hex("#1b1d22");
        p.title_inactive = hex("#8b8f98");
        p.border_active = theme.accent.with_alpha(0.85F);
        p.border_inactive = hex("#c9ccd2");
    }
    return p;
}

} // namespace tetra::theme
