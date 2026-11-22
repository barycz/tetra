#include "layer_surface.hpp"

#include "output.hpp"
#include "popup.hpp"
#include "server.hpp"

namespace tetra {

LayerSurface::LayerSurface(Server& server, Output& output, wlr_layer_surface_v1* layer_surface)
    : server_(server), output_(output), layer_surface_(layer_surface),
      scene_(wlr_scene_layer_surface_v1_create(server.layer_tree(layer_surface->pending.layer),
                                               layer_surface)),
      layer_(layer_surface->pending.layer), descriptor_{SceneDescriptor::Role::LayerSurface, this} {
    scene_->tree->node.data = &descriptor_;

    popup_parent_ = {
        .tree = scene_->tree,
        .constraint =
            [this] {
                wlr_box box = output_.box();
                box.x -= scene_->tree->node.x;
                box.y -= scene_->tree->node.y;
                return box;
            },
    };
    layer_surface_->data = &popup_parent_;

    map_.connect(layer_surface_->surface->events.map, [this] {
        output_.arrange_layers();
        server_.handle_layer_mapped(*this);
    });
    unmap_.connect(layer_surface_->surface->events.unmap, [this] {
        output_.arrange_layers();
        server_.handle_layer_unmapped(*this);
    });
    commit_.connect(layer_surface_->surface->events.commit, [this] {
        const bool layer_changed = layer_surface_->current.layer != layer_;
        if (layer_changed) {
            layer_ = layer_surface_->current.layer;
            wlr_scene_node_reparent(&scene_->tree->node, server_.layer_tree(layer_));
        }
        if (layer_surface_->initial_commit || layer_changed ||
            layer_surface_->current.committed != 0) {
            output_.arrange_layers();
            if (mapped()) {
                server_.handle_layer_changed(*this);
            }
        }
    });
    destroy_.connect(layer_surface_->events.destroy,
                     [this] { output_.remove_layer_surface(*this); });
    new_popup_.connect<wlr_xdg_popup>(layer_surface_->events.new_popup, [this](auto* popup) {
        Popup::create(popup, popup_parent_);
    });
}

} // namespace tetra
