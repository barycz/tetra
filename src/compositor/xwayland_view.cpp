#include "xwayland_view.hpp"

#include "server.hpp"

namespace tetra {

// --- XwaylandView ----------------------------------------------------------------

XwaylandView::XwaylandView(Server& server, wlr_xwayland_surface* xsurface)
    : View(server), xsurface_(xsurface) {
    content_ = {.x = xsurface->x, .y = xsurface->y, .width = xsurface->width,
                .height = xsurface->height};

    destroy_.connect(xsurface_->events.destroy, [this] { server_.destroy_view(*this); });
    associate_.connect(xsurface_->events.associate, [this] { handle_associate(); });
    dissociate_.connect(xsurface_->events.dissociate, [this] { handle_dissociate(); });

    request_configure_.connect<wlr_xwayland_surface_configure_event>(
        xsurface_->events.request_configure, [this](auto* event) {
            if (!mapped()) {
                // Before mapping, X11 clients get what they ask for.
                wlr_xwayland_surface_configure(xsurface_, event->x, event->y, event->width,
                                               event->height);
                return;
            }
            if (state() != State::Normal) {
                refit();
                return;
            }
            // Mapped floating window: honour size, keep our position.
            configure({.x = content_.x, .y = content_.y, .width = event->width,
                       .height = event->height});
        });
    request_move_.connect(xsurface_->events.request_move,
                          [this] { server_.begin_interactive_move(*this); });
    request_resize_.connect<wlr_xwayland_resize_event>(
        xsurface_->events.request_resize,
        [this](auto* event) { server_.begin_interactive_resize(*this, event->edges); });
    request_maximize_.connect(xsurface_->events.request_maximize, [this] {
        set_maximized(xsurface_->maximized_horz || xsurface_->maximized_vert);
    });
    request_fullscreen_.connect(xsurface_->events.request_fullscreen,
                                [this] { set_fullscreen(xsurface_->fullscreen); });
    request_minimize_.connect<wlr_xwayland_minimize_event>(
        xsurface_->events.request_minimize, [this](auto* event) {
            if (mapped()) {
                set_minimized(event->minimize);
            }
        });
    request_activate_.connect(xsurface_->events.request_activate, [this] {
        if (mapped()) {
            server_.focus_view(this);
        }
    });
    set_title_.connect(xsurface_->events.set_title, [this] { handle_title_changed(); });
    set_class_.connect(xsurface_->events.set_class, [this] { handle_title_changed(); });
    set_decorations_.connect(xsurface_->events.set_decorations, [this] { update_decorated(); });
}

void XwaylandView::handle_associate() {
    wlr_surface* surface = xsurface_->surface;
    map_.connect(surface->events.map, [this] {
        attach_surface_tree(wlr_scene_subsurface_tree_create(tree(), xsurface_->surface));
        update_decorated();
        // X11 windows may pick their own position (dialogs, restored
        // sessions); respect it if they set one.
        const bool positioned = xsurface_->x != 0 || xsurface_->y != 0;
        if (positioned) {
            move_to(xsurface_->x, xsurface_->y);
        }
        handle_map(xsurface_->maximized_horz && xsurface_->maximized_vert,
                   xsurface_->fullscreen, /*place=*/!positioned);
    });
    unmap_.connect(surface->events.unmap, [this] {
        handle_unmap();
        detach_surface_tree();
    });
    commit_.connect(surface->events.commit, [this] { handle_commit(); });
}

void XwaylandView::handle_dissociate() {
    map_.disconnect();
    unmap_.disconnect();
    commit_.disconnect();
}

void XwaylandView::update_decorated() {
    // Motif hints: clients drawing their own frame ask for no title/border.
    set_decorated(xsurface_->decorations == WLR_XWAYLAND_SURFACE_DECORATIONS_ALL);
}

std::string_view XwaylandView::title() const {
    return xsurface_->title != nullptr ? xsurface_->title : "";
}

std::string_view XwaylandView::app_id() const {
    return xsurface_->class_ != nullptr ? xsurface_->class_ : "";
}

void XwaylandView::close() {
    wlr_xwayland_surface_close(xsurface_);
}

void XwaylandView::configure(const wlr_box& content) {
    wlr_xwayland_surface_configure(xsurface_, static_cast<int16_t>(content.x),
                                   static_cast<int16_t>(content.y),
                                   static_cast<uint16_t>(content.width),
                                   static_cast<uint16_t>(content.height));
}

void XwaylandView::send_activated(bool activated) {
    wlr_xwayland_surface_activate(xsurface_, activated);
    if (activated) {
        wlr_xwayland_surface_restack(xsurface_, nullptr, XCB_STACK_MODE_ABOVE);
    }
}

void XwaylandView::send_maximized(bool maximized) {
    wlr_xwayland_surface_set_maximized(xsurface_, maximized, maximized);
}

void XwaylandView::send_fullscreen(bool fullscreen) {
    wlr_xwayland_surface_set_fullscreen(xsurface_, fullscreen);
}

void XwaylandView::send_minimized(bool minimized) {
    wlr_xwayland_surface_set_minimized(xsurface_, minimized);
}

void XwaylandView::send_position() {
    // X11 clients need their root position (menus are placed from it);
    // layout coordinates are X11 root coordinates.
    wlr_xwayland_surface_configure(xsurface_, static_cast<int16_t>(content_.x),
                                   static_cast<int16_t>(content_.y), xsurface_->width,
                                   xsurface_->height);
}

wlr_box XwaylandView::surface_geometry() const {
    return {.x = 0, .y = 0, .width = xsurface_->width, .height = xsurface_->height};
}

// --- XwaylandUnmanaged -----------------------------------------------------------

XwaylandUnmanaged::XwaylandUnmanaged(Server& server, wlr_xwayland_surface* xsurface)
    : server_(server), xsurface_(xsurface) {
    destroy_.connect(xsurface_->events.destroy, [this] { delete this; }); // NOLINT
    request_configure_.connect<wlr_xwayland_surface_configure_event>(
        xsurface_->events.request_configure, [this](auto* event) {
            wlr_xwayland_surface_configure(xsurface_, event->x, event->y, event->width,
                                           event->height);
        });
    set_geometry_.connect(xsurface_->events.set_geometry, [this] {
        if (tree_ != nullptr) {
            wlr_scene_node_set_position(&tree_->node, xsurface_->x, xsurface_->y);
        }
    });
    associate_.connect(xsurface_->events.associate, [this] {
        map_.connect(xsurface_->surface->events.map, [this] {
            tree_ = wlr_scene_subsurface_tree_create(server_.layers.unmanaged, xsurface_->surface);
            wlr_scene_node_set_position(&tree_->node, xsurface_->x, xsurface_->y);
            if (wlr_xwayland_surface_override_redirect_wants_focus(xsurface_)) {
                server_.set_keyboard_focus(xsurface_->surface);
            }
        });
        unmap_.connect(xsurface_->surface->events.unmap, [this] {
            if (tree_ != nullptr) {
                wlr_scene_node_destroy(&tree_->node);
                tree_ = nullptr;
            }
            server_.restore_keyboard_focus();
        });
    });
    dissociate_.connect(xsurface_->events.dissociate, [this] {
        map_.disconnect();
        unmap_.disconnect();
    });
}

XwaylandUnmanaged::~XwaylandUnmanaged() {
    if (tree_ != nullptr) {
        wlr_scene_node_destroy(&tree_->node);
    }
}

} // namespace tetra
