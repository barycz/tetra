#pragma once

#include "wlr.hpp"

#include <functional>

namespace tetra {

// What a popup needs from whatever it is attached to (view, layer surface or
// another popup). Stored in wlr_xdg_surface::data / wlr_layer_surface_v1::data.
struct PopupParent {
    wlr_scene_tree* tree = nullptr;
    // Area popups must stay inside, in the root surface's coordinate space
    // (relative to the toplevel's window geometry / the layer surface origin).
    std::function<wlr_box()> constraint;
};

class LayerSurface;
class View;

// Attached to scene nodes (node.data) that root a compositor object, so a hit
// on any descendant node can be traced back to its owner.
struct SceneDescriptor {
    enum class Role { View, Decoration, LayerSurface };
    Role role;
    void* object;
};

// Nearest descriptor on the path from `node` up to the scene root.
inline SceneDescriptor* find_descriptor(wlr_scene_node* node) {
    for (; node != nullptr; node = node->parent != nullptr ? &node->parent->node : nullptr) {
        if (node->data != nullptr) {
            return static_cast<SceneDescriptor*>(node->data);
        }
    }
    return nullptr;
}

} // namespace tetra
