#include "popup.hpp"

namespace tetra {

void Popup::create(wlr_xdg_popup* popup, const PopupParent& parent) {
    new Popup(popup, parent); // NOLINT(cppcoreguidelines-owning-memory): owned by destroy signal
}

Popup::Popup(wlr_xdg_popup* popup, const PopupParent& parent)
    : popup_(popup),
      self_{.tree = wlr_scene_xdg_surface_create(parent.tree, popup->base),
            .constraint = parent.constraint} {
    popup_->base->data = &self_;

    commit_.connect(popup_->base->surface->events.commit, [this] {
        if (popup_->base->initial_commit) {
            // Keep menus on screen; this also schedules the configure.
            if (self_.constraint) {
                const wlr_box box = self_.constraint();
                wlr_xdg_popup_unconstrain_from_box(popup_, &box);
            }
            wlr_xdg_surface_schedule_configure(popup_->base);
        }
    });
    destroy_.connect(popup_->events.destroy, [this] { delete this; }); // NOLINT
}

} // namespace tetra
