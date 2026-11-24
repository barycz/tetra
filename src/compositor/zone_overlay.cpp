#include "zone_overlay.hpp"

#include "cairo_buffer.hpp"
#include "server.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <ranges>
#include <string>

namespace tetra {

namespace {

void rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
    r = std::min({r, w / 2, h / 2});
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -std::numbers::pi / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, std::numbers::pi / 2);
    cairo_arc(cr, x + r, y + h - r, r, std::numbers::pi / 2, std::numbers::pi);
    cairo_arc(cr, x + r, y + r, r, std::numbers::pi, 3 * std::numbers::pi / 2);
    cairo_close_path(cr);
}

} // namespace

ZoneOverlay::ZoneOverlay(Server& server, wlr_scene_tree* parent)
    : server_(server), buffer_(wlr_scene_buffer_create(parent, nullptr)) {
    // Drawn over windows, but never in the way of the pointer.
    buffer_->point_accepts_input = [](wlr_scene_buffer*, double*, double*) { return false; };
    wlr_scene_node_set_enabled(&buffer_->node, false);
}

ZoneOverlay::~ZoneOverlay() {
    wlr_scene_node_destroy(&buffer_->node);
}

void ZoneOverlay::hide() {
    if (output_ != nullptr) {
        wlr_scene_node_set_enabled(&buffer_->node, false);
        wlr_scene_buffer_set_buffer(buffer_, nullptr);
        output_ = nullptr;
        zones_.clear();
        picked_.clear();
    }
}

void ZoneOverlay::show(wlr_output* output, std::span<const zones::Box> zones, std::span<const std::size_t> picked) {
    if (zones.empty()) {
        hide();
        return;
    }
    if (output == output_ && std::ranges::equal(zones, zones_) && std::ranges::equal(picked, picked_)) {
        return;
    }
    output_ = output;
    zones_.assign(zones.begin(), zones.end());
    picked_.assign(picked.begin(), picked.end());

    std::vector<std::size_t> every(zones_.size());
    for (std::size_t i = 0; i < every.size(); ++i) {
        every[i] = i;
    }
    const zones::Box all = zones::span(zones_, every);
    const double scale = output->scale > 0 ? output->scale : 1.0;
    const auto width = static_cast<int>(std::ceil(all.width * scale));
    const auto height = static_cast<int>(std::ceil(all.height * scale));
    CairoBuffer* buffer = CairoBuffer::create(width, height);
    if (buffer == nullptr) {
        hide();
        return;
    }
    cairo_t* cr = cairo_create(buffer->surface());
    cairo_scale(cr, scale, scale);
    const auto& accent = server_.palette().accent;
    const auto& text = server_.palette().text;
    const double radius = 8;
    for (std::size_t i = 0; i < zones_.size(); ++i) {
        const zones::Box& z = zones_[i];
        const bool on = std::ranges::contains(picked_, i);
        const double x = z.x - all.x;
        const double y = z.y - all.y;
        rounded_rect(cr, x + 1, y + 1, z.width - 2, z.height - 2, radius);
        cairo_set_source_rgba(cr, accent.r, accent.g, accent.b, on ? 0.45 : 0.12);
        cairo_fill_preserve(cr);
        cairo_set_line_width(cr, 2);
        cairo_set_source_rgba(cr, accent.r, accent.g, accent.b, on ? 1.0 : 0.6);
        cairo_stroke(cr);

        // The zone's number, for Super+Left/Right and finding your way.
        const std::string number = std::to_string(i + 1);
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, std::clamp(std::min(z.width, z.height) / 5.0, 14.0, 48.0));
        cairo_text_extents_t extents{};
        cairo_text_extents(cr, number.c_str(), &extents);
        cairo_move_to(cr, x + ((z.width - extents.width) / 2) - extents.x_bearing,
                      y + ((z.height - extents.height) / 2) - extents.y_bearing);
        cairo_set_source_rgba(cr, text.r, text.g, text.b, on ? 0.95 : 0.6);
        cairo_show_text(cr, number.c_str());
    }
    cairo_destroy(cr);
    cairo_surface_flush(buffer->surface());

    wlr_scene_buffer_set_buffer(buffer_, buffer->base());
    wlr_buffer_drop(buffer->base());
    wlr_scene_buffer_set_dest_size(buffer_, all.width, all.height);
    wlr_scene_node_set_position(&buffer_->node, all.x, all.y);
    wlr_scene_node_set_enabled(&buffer_->node, true);
}

} // namespace tetra
