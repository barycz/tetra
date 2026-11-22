#pragma once

#include "listener.hpp"
#include "scene.hpp"

namespace tetra {

class Output;
class Server;

// A wlr-layer-shell surface: panels, bars, wallpapers, lock screens, OSDs.
// Owned by its Output.
class LayerSurface {
public:
    LayerSurface(Server& server, Output& output, wlr_layer_surface_v1* layer_surface);
    ~LayerSurface() = default;

    LayerSurface(const LayerSurface&) = delete;
    LayerSurface& operator=(const LayerSurface&) = delete;
    LayerSurface(LayerSurface&&) = delete;
    LayerSurface& operator=(LayerSurface&&) = delete;

    [[nodiscard]] wlr_layer_surface_v1* wlr() const noexcept { return layer_surface_; }
    [[nodiscard]] wlr_scene_layer_surface_v1* scene() const noexcept { return scene_; }
    [[nodiscard]] Output& output() const noexcept { return output_; }
    [[nodiscard]] bool mapped() const noexcept { return layer_surface_->surface->mapped; }
    [[nodiscard]] zwlr_layer_shell_v1_layer layer() const noexcept {
        return layer_surface_->current.layer;
    }
    [[nodiscard]] zwlr_layer_surface_v1_keyboard_interactivity keyboard() const noexcept {
        return layer_surface_->current.keyboard_interactive;
    }

private:
    Server& server_;
    Output& output_;
    wlr_layer_surface_v1* layer_surface_;
    wlr_scene_layer_surface_v1* scene_;
    zwlr_layer_shell_v1_layer layer_;
    SceneDescriptor descriptor_;
    PopupParent popup_parent_;

    Listener map_;
    Listener unmap_;
    Listener commit_;
    Listener destroy_;
    Listener new_popup_;
};

} // namespace tetra
