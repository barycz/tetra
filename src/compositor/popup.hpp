#pragma once

#include "listener.hpp"
#include "scene.hpp"

namespace tetra {

// An xdg_popup (menus, tooltips) of a view, layer surface or another popup.
// Rendered as part of its parent's scene tree. Deletes itself on destroy.
class Popup {
public:
    static void create(wlr_xdg_popup* popup, const PopupParent& parent);

    Popup(const Popup&) = delete;
    Popup& operator=(const Popup&) = delete;
    Popup(Popup&&) = delete;
    Popup& operator=(Popup&&) = delete;

private:
    Popup(wlr_xdg_popup* popup, const PopupParent& parent);
    ~Popup() = default;

    wlr_xdg_popup* popup_;
    PopupParent self_; // parent info for this popup's own children
    Listener commit_;
    Listener destroy_;
};

} // namespace tetra
