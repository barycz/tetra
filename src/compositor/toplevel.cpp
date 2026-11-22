#include "toplevel.hpp"

#include "server.hpp"

#include <algorithm>

namespace tetra {

Toplevel::Toplevel(Server& server, wlr_xdg_toplevel* xdg_toplevel)
    : server_(server), xdg_(xdg_toplevel),
      tree_(wlr_scene_xdg_surface_create(server.layers.windows, xdg_toplevel->base)) {
    // node.data marks the root of a toplevel's subtree (see Server::toplevel_at);
    // base->data lets popups find their parent scene tree.
    tree_->node.data = this;
    xdg_->base->data = tree_;

    wlr_surface* surface = xdg_->base->surface;
    map_.connect(surface->events.map, [this] { handle_map(); });
    unmap_.connect(surface->events.unmap, [this] { server_.handle_toplevel_unmapped(*this); });
    commit_.connect(surface->events.commit, [this] {
        if (xdg_->base->initial_commit) {
            // Reply to the initial commit; 0x0 lets the client pick its size.
            wlr_xdg_toplevel_set_size(xdg_, 0, 0);
        }
    });
    destroy_.connect(xdg_->events.destroy, [this] { server_.destroy_toplevel(*this); });

    request_move_.connect(xdg_->events.request_move,
                          [this] { server_.begin_interactive_move(*this); });
    request_resize_.connect<wlr_xdg_toplevel_resize_event>(
        xdg_->events.request_resize,
        [this](auto* event) { server_.begin_interactive_resize(*this, event->edges); });

    // Maximize/fullscreen are not implemented yet, but xdg-shell requires a
    // configure in response (only once the surface is initialised).
    auto ack_unsupported = [this] {
        if (xdg_->base->initialized) {
            wlr_xdg_surface_schedule_configure(xdg_->base);
        }
    };
    request_maximize_.connect(xdg_->events.request_maximize, ack_unsupported);
    request_fullscreen_.connect(xdg_->events.request_fullscreen, ack_unsupported);
}

void Toplevel::set_position(int x, int y) {
    wlr_scene_node_set_position(&tree_->node, x, y);
}

void Toplevel::set_activated(bool activated) {
    wlr_xdg_toplevel_set_activated(xdg_, activated);
}

void Toplevel::raise() {
    wlr_scene_node_raise_to_top(&tree_->node);
}

void Toplevel::handle_map() {
    if (!placed_) {
        center_on_active_output();
        placed_ = true;
    }
    server_.handle_toplevel_mapped(*this);
}

void Toplevel::center_on_active_output() {
    wlr_output* output =
        wlr_output_layout_output_at(server_.output_layout, server_.cursor->x, server_.cursor->y);
    if (output == nullptr) {
        output = wlr_output_layout_get_center_output(server_.output_layout);
    }
    if (output == nullptr) {
        return;
    }

    wlr_box area{};
    wlr_output_layout_get_box(server_.output_layout, output, &area);
    const wlr_box& geo = geometry();
    // Keep the top-left corner on screen even for windows larger than the output.
    const int x = std::max(area.x, area.x + ((area.width - geo.width) / 2));
    const int y = std::max(area.y, area.y + ((area.height - geo.height) / 2));
    set_position(x - geo.x, y - geo.y);
}

} // namespace tetra
