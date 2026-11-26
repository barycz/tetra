#include "xdg_view.hpp"

#include "server.hpp"

namespace tetra {

XdgView::XdgView(Server& server, wlr_xdg_toplevel* toplevel) : View(server), toplevel_(toplevel) {
    wlr_scene_tree* surface_tree = wlr_scene_xdg_surface_create(tree(), toplevel_->base);
    attach_surface_tree(surface_tree);
    // Popups find their parent tree and screen constraint through base->data.
    popup_parent_ = {
        .tree = surface_tree,
        .constraint =
            [this] {
                const wlr_box area = usable_area();
                return wlr_box{.x = area.x - content_.x, .y = area.y - content_.y,
                               .width = area.width, .height = area.height};
            },
    };
    toplevel_->base->data = &popup_parent_;

    wlr_surface* surface = toplevel_->base->surface;
    map_.connect(surface->events.map, [this] {
        handle_map(toplevel_->requested.maximized, toplevel_->requested.fullscreen);
    });
    unmap_.connect(surface->events.unmap, [this] { handle_unmap(); });
    commit_.connect(surface->events.commit, [this] {
        if (toplevel_->base->initial_commit) {
            // The remembered size, or 0x0 to let the client choose.
            const auto size = prepare_placement();
            wlr_xdg_toplevel_set_size(toplevel_, size ? size->first : 0, size ? size->second : 0);
            apply_decoration_mode();
            return;
        }
        handle_commit();
    });
    destroy_.connect(toplevel_->events.destroy, [this] { server_.destroy_view(*this); });

    request_move_.connect(toplevel_->events.request_move,
                          [this] { server_.begin_interactive_move(*this); });
    request_resize_.connect<wlr_xdg_toplevel_resize_event>(
        toplevel_->events.request_resize,
        [this](auto* event) { server_.begin_interactive_resize(*this, event->edges); });
    request_maximize_.connect(toplevel_->events.request_maximize, [this] {
        if (toplevel_->base->initialized && mapped()) {
            set_maximized(toplevel_->requested.maximized);
        } else if (toplevel_->base->initialized) {
            wlr_xdg_surface_schedule_configure(toplevel_->base);
        }
    });
    request_fullscreen_.connect(toplevel_->events.request_fullscreen, [this] {
        if (toplevel_->base->initialized && mapped()) {
            set_fullscreen(toplevel_->requested.fullscreen);
        } else if (toplevel_->base->initialized) {
            wlr_xdg_surface_schedule_configure(toplevel_->base);
        }
    });
    // A parent set after mapping (xdg-foreign's set_parent_of): above it.
    set_parent_.connect(toplevel_->events.set_parent, [this] {
        if (const View* parent = parent_view(); parent != nullptr && mapped()) {
            raise();
        }
    });
    request_minimize_.connect(toplevel_->events.request_minimize, [this] {
        if (mapped() && toplevel_->requested.minimized) {
            set_minimized(true);
        }
    });
    set_title_.connect(toplevel_->events.set_title, [this] { handle_title_changed(); });
    set_app_id_.connect(toplevel_->events.set_app_id, [this] { handle_title_changed(); });
}

XdgView::~XdgView() {
    decoration_destroy_.disconnect();
    decoration_request_mode_.disconnect();
}

std::string_view XdgView::title() const {
    return toplevel_->title != nullptr ? toplevel_->title : "";
}

std::string_view XdgView::app_id() const {
    return toplevel_->app_id != nullptr ? toplevel_->app_id : "";
}

void XdgView::close() {
    wlr_xdg_toplevel_send_close(toplevel_);
}

void XdgView::set_xdg_decoration(wlr_xdg_toplevel_decoration_v1* decoration) {
    xdg_decoration_ = decoration;
    decoration_destroy_.connect(decoration->events.destroy, [this] {
        decoration_destroy_.disconnect();
        decoration_request_mode_.disconnect();
        xdg_decoration_ = nullptr;
        set_decorated(false);
    });
    decoration_request_mode_.connect(decoration->events.request_mode,
                                     [this] { apply_decoration_mode(); });
    apply_decoration_mode();
}

void XdgView::apply_decoration_mode() {
    // The mode may only be sent once the surface is initialised; this is
    // called again from the initial commit.
    if (xdg_decoration_ == nullptr || !toplevel_->base->initialized) {
        return;
    }
    wlr_xdg_toplevel_decoration_v1_set_mode(xdg_decoration_,
                                            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    set_decorated(true);
}

void XdgView::configure(const wlr_box& content) {
    wlr_xdg_toplevel_set_size(toplevel_, content.width, content.height);
}

void XdgView::send_activated(bool activated) {
    wlr_xdg_toplevel_set_activated(toplevel_, activated);
}

void XdgView::send_maximized(bool maximized) {
    wlr_xdg_toplevel_set_maximized(toplevel_, maximized);
}

void XdgView::send_fullscreen(bool fullscreen) {
    wlr_xdg_toplevel_set_fullscreen(toplevel_, fullscreen);
}

wlr_box XdgView::surface_geometry() const {
    return toplevel_->base->geometry;
}

View* XdgView::parent_view() const {
    if (toplevel_->parent == nullptr) {
        return nullptr;
    }
    for (const auto& view : server_.views()) {
        const auto* xdg = dynamic_cast<const XdgView*>(view.get());
        if (xdg != nullptr && xdg->toplevel() == toplevel_->parent) {
            return view.get();
        }
    }
    return nullptr;
}

} // namespace tetra
