#include "decoration.hpp"

#include "cairo_buffer.hpp"
#include "server.hpp"
#include "view.hpp"

#include <pango/pangocairo.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace tetra {

namespace {

constexpr int kButtonWidth = 46;  // Windows-style caption buttons
constexpr int kResizeMargin = 6;  // invisible grab area outside the border
constexpr int kCornerSize = 16;   // edge length that counts as a corner
constexpr double kGlyphSize = 10; // caption button glyphs
constexpr int kTitlePadding = 12;

void set_source(cairo_t* cr, const config::Color& c) {
    cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
}

} // namespace

Decoration::Decoration(Server& server, View& view)
    : server_(server), view_(view), descriptor_{SceneDescriptor::Role::Decoration, &view},
      tree_(wlr_scene_tree_create(view.tree())) {
    tree_->node.data = &descriptor_;
    // Below the client's surfaces, whatever order they were attached in.
    wlr_scene_node_lower_to_bottom(&tree_->node);

    constexpr float transparent[4] = {0, 0, 0, 0};
    resize_margin_ = wlr_scene_rect_create(tree_, 0, 0, transparent);
    for (auto*& side : borders_) {
        side = wlr_scene_rect_create(tree_, 0, 0, transparent);
    }
    titlebar_ = wlr_scene_buffer_create(tree_, nullptr);
}

Decoration::~Decoration() {
    wlr_scene_node_destroy(&tree_->node);
    if (title_buffer_ != nullptr) {
        wlr_buffer_unlock(title_buffer_);
    }
}

int Decoration::border() const {
    return view_.state() == View::State::Normal ? server_.config().windows.border_width : 0;
}

Decoration::Extents Decoration::extents() const {
    if (!visible_) {
        return {};
    }
    const int b = border();
    return {.top = server_.config().windows.titlebar_height + b, .left = b, .right = b, .bottom = b};
}

float Decoration::render_scale() const {
    if (wlr_output* output = view_.output()) {
        return output->scale;
    }
    return 1.0F;
}

void Decoration::set_visible(bool visible) {
    visible_ = visible;
    wlr_scene_node_set_enabled(&tree_->node, visible);
}

void Decoration::set_opacity(float opacity) {
    if (opacity == opacity_) {
        return;
    }
    opacity_ = opacity;
    const auto& palette = server_.palette();
    auto color = (view_.activated() ? palette.border_active : palette.border_inactive).premultiplied();
    for (float& channel : color) {
        channel *= opacity_;
    }
    for (wlr_scene_rect* border : borders_) {
        wlr_scene_rect_set_color(border, color.data());
    }
}

void Decoration::set_hover(Part part) {
    auto is_button = [](Part p) {
        return p == Part::Minimize || p == Part::Maximize || p == Part::Close;
    };
    if (part == hover_) {
        return;
    }
    const bool redraw = is_button(part) || is_button(hover_);
    hover_ = part;
    if (redraw) {
        update();
    }
}

void Decoration::update() {
    const wlr_box& c = view_.content();
    if (!visible_ || c.width <= 0 || c.height <= 0) {
        return;
    }
    const auto& palette = server_.palette();
    const int title = server_.config().windows.titlebar_height;
    const int b = border();
    const bool active = view_.activated();

    // Border: four strips around title bar + content, so translucent clients
    // do not show a solid rectangle behind them.
    auto color = (active ? palette.border_active : palette.border_inactive).premultiplied();
    for (float& channel : color) {
        channel *= opacity_;
    }
    const int outer_w = c.width + (2 * b);
    const std::array<wlr_box, 4> strips = {{
        {.x = -b, .y = -title - b, .width = outer_w, .height = b}, // top
        {.x = -b, .y = c.height, .width = outer_w, .height = b},   // bottom
        {.x = -b, .y = -title, .width = b, .height = c.height + title}, // left
        {.x = c.width, .y = -title, .width = b, .height = c.height + title}, // right
    }};
    for (std::size_t i = 0; i < strips.size(); ++i) {
        wlr_scene_node_set_position(&borders_.at(i)->node, strips.at(i).x, strips.at(i).y);
        wlr_scene_rect_set_size(borders_.at(i), strips.at(i).width, strips.at(i).height);
        wlr_scene_rect_set_color(borders_.at(i), color.data());
    }

    const bool resizable = view_.state() == View::State::Normal;
    wlr_scene_node_set_enabled(&resize_margin_->node, resizable);
    const int m = kResizeMargin;
    wlr_scene_node_set_position(&resize_margin_->node, -b - m, -title - b - m);
    wlr_scene_rect_set_size(resize_margin_, c.width + (2 * (b + m)),
                            c.height + title + (2 * (b + m)));

    wlr_scene_node_set_position(&titlebar_->node, 0, -title);
    render_titlebar(c.width, title, render_scale());
}

void Decoration::render_titlebar(int width, int height, float scale) {
    const auto& palette = server_.palette();
    const auto& theme = server_.config().theme;
    const bool active = view_.activated();
    const config::Color& text = active ? palette.title_active : palette.title_inactive;

    auto* buffer = CairoBuffer::create(static_cast<int>(std::ceil(width * scale)),
                                       static_cast<int>(std::ceil(height * scale)));
    cairo_t* cr = cairo_create(buffer->surface());
    cairo_scale(cr, scale, scale);

    set_source(cr, active ? palette.titlebar_active : palette.titlebar_inactive);
    cairo_paint(cr);

    // Caption buttons, right to left: close, maximize/restore, minimize.
    const std::array<Part, 3> buttons = {Part::Close, Part::Maximize, Part::Minimize};
    int buttons_width = 0;
    cairo_set_line_width(cr, 1.0);
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        const Part part = buttons.at(i);
        const int x0 = width - (static_cast<int>(i) + 1) * kButtonWidth;
        if (x0 < 0) {
            break;
        }
        buttons_width += kButtonWidth;
        const bool hovered = hover_ == part;
        if (hovered) {
            set_source(cr, part == Part::Close ? palette.close_hover : palette.hover);
            cairo_rectangle(cr, x0, 0, kButtonWidth, height);
            cairo_fill(cr);
        }
        set_source(cr, hovered && part == Part::Close ? config::Color{1, 1, 1, 1} : text);

        // Glyphs on half-pixel coordinates for crisp 1px lines.
        const double cx = std::floor(x0 + (kButtonWidth / 2.0)) + 0.5;
        const double cy = std::floor(height / 2.0) + 0.5;
        const double h = kGlyphSize / 2.0;
        switch (part) {
        case Part::Minimize:
            cairo_move_to(cr, cx - h, cy);
            cairo_line_to(cr, cx + h, cy);
            break;
        case Part::Maximize:
            if (view_.state() == View::State::Maximized) {
                // Restore: two overlapping squares.
                cairo_rectangle(cr, cx - h, cy - h + 2, kGlyphSize - 2, kGlyphSize - 2);
                cairo_move_to(cr, cx - h + 2, cy - h + 2);
                cairo_line_to(cr, cx - h + 2, cy - h);
                cairo_line_to(cr, cx + h, cy - h);
                cairo_line_to(cr, cx + h, cy + h - 2);
                cairo_line_to(cr, cx + h - 2, cy + h - 2);
            } else {
                cairo_rectangle(cr, cx - h, cy - h, kGlyphSize, kGlyphSize);
            }
            break;
        case Part::Close:
            cairo_move_to(cr, cx - h, cy - h);
            cairo_line_to(cr, cx + h, cy + h);
            cairo_move_to(cr, cx + h, cy - h);
            cairo_line_to(cr, cx - h, cy + h);
            break;
        default:
            break;
        }
        cairo_stroke(cr);
    }

    // Title, ellipsized to the space left of the buttons.
    const int available = width - buttons_width - (2 * kTitlePadding);
    const std::string_view title = view_.title();
    if (available > 0 && !title.empty()) {
        PangoLayout* layout = pango_cairo_create_layout(cr);
        PangoFontDescription* font = pango_font_description_from_string(theme.font.c_str());
        pango_font_description_set_absolute_size(
            font, std::round(theme.font_size * 0.93) * PANGO_SCALE);
        pango_layout_set_font_description(layout, font);
        pango_layout_set_text(layout, title.data(), static_cast<int>(title.size()));
        pango_layout_set_width(layout, available * PANGO_SCALE);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_single_paragraph_mode(layout, TRUE);

        int text_w = 0;
        int text_h = 0;
        pango_layout_get_pixel_size(layout, &text_w, &text_h);
        set_source(cr, text);
        cairo_move_to(cr, kTitlePadding, std::round((height - text_h) / 2.0));
        pango_cairo_show_layout(cr, layout);

        pango_font_description_free(font);
        g_object_unref(layout);
    }

    cairo_destroy(cr);
    cairo_surface_flush(buffer->surface());

    wlr_scene_buffer_set_buffer(titlebar_, buffer->base());
    wlr_scene_buffer_set_dest_size(titlebar_, width, height);
    if (title_buffer_ != nullptr) {
        wlr_buffer_unlock(title_buffer_);
    }
    title_buffer_ = wlr_buffer_lock(buffer->base());
    wlr_buffer_drop(buffer->base());
}

Decoration::Hit Decoration::hit_test(double x, double y) const {
    const wlr_box& c = view_.content();
    const int title = server_.config().windows.titlebar_height;

    if (y >= -title && y < 0 && x >= 0 && x < c.width) {
        const double from_right = c.width - x;
        const std::array<Part, 3> buttons = {Part::Close, Part::Maximize, Part::Minimize};
        for (std::size_t i = 0; i < buttons.size(); ++i) {
            const int edge = (static_cast<int>(i) + 1) * kButtonWidth;
            if (edge > c.width) {
                break;
            }
            if (from_right <= edge) {
                return {.part = buttons.at(i)};
            }
        }
        return {.part = Part::Titlebar};
    }

    if (view_.state() != View::State::Normal) {
        return {};
    }
    uint32_t edges = WLR_EDGE_NONE;
    if (x < 0) {
        edges |= WLR_EDGE_LEFT;
    } else if (x >= c.width) {
        edges |= WLR_EDGE_RIGHT;
    }
    if (y < -title) {
        edges |= WLR_EDGE_TOP;
    } else if (y >= c.height) {
        edges |= WLR_EDGE_BOTTOM;
    }
    // Generous corners: near the end of a side, resize diagonally.
    if (edges == WLR_EDGE_LEFT || edges == WLR_EDGE_RIGHT) {
        if (y < -title + kCornerSize) {
            edges |= WLR_EDGE_TOP;
        } else if (y >= c.height - kCornerSize) {
            edges |= WLR_EDGE_BOTTOM;
        }
    } else if (edges == WLR_EDGE_TOP || edges == WLR_EDGE_BOTTOM) {
        if (x < kCornerSize) {
            edges |= WLR_EDGE_LEFT;
        } else if (x >= c.width - kCornerSize) {
            edges |= WLR_EDGE_RIGHT;
        }
    }
    if (edges == WLR_EDGE_NONE) {
        return {};
    }
    return {.part = Part::Edge, .edges = edges};
}

const char* Decoration::cursor_for(const Hit& hit) {
    if (hit.part == Part::Edge) {
        return wlr_xcursor_get_resize_name(static_cast<wlr_edges>(hit.edges));
    }
    return "default";
}

} // namespace tetra
