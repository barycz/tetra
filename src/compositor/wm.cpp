// Window management: outputs and layers, focus, view lifecycle.

#include "layer_surface.hpp"
#include "output.hpp"
#include "server.hpp"
#include "session_lock.hpp"
#include "view.hpp"

#include <algorithm>
#include <array>

namespace tetra {

// --- outputs & layers ------------------------------------------------------------

Output* Server::find_output(wlr_output* wlr_output) const {
    for (const auto& output : outputs_) {
        if (output->wlr() == wlr_output) {
            return output.get();
        }
    }
    return nullptr;
}

wlr_box Server::usable_area_at_cursor() const {
    wlr_output* under = wlr_output_layout_output_at(output_layout, cursor->x, cursor->y);
    if (const Output* output = find_output(under)) {
        return output->usable_area();
    }
    for (const auto& output : outputs_) {
        if (!wlr_box_empty(&output->usable_area())) {
            return output->usable_area();
        }
    }
    return {};
}

wlr_scene_tree* Server::layer_tree(zwlr_layer_shell_v1_layer layer) const {
    switch (layer) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
        return layers.background;
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
        return layers.bottom;
    case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
        return layers.top;
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
        return layers.overlay;
    }
    return layers.top;
}

void Server::handle_usable_area_changed(Output& output) {
    if (arranging_outputs_) {
        return; // apply_config fits every window afterwards
    }
    for (const auto& view : views_) {
        if (view->mapped() && view->output() == output.wlr()) {
            view->refit();
        }
    }
}

void Server::destroy_output(Output& output) {
    if (session_lock_) {
        session_lock_->handle_output_destroyed(output.wlr());
    }
    std::erase_if(outputs_, [&](const auto& ptr) { return ptr.get() == &output; });
    schedule_output_manager_update();
    // Rescue windows that were only visible on the removed output.
    const wlr_box area = usable_area_at_cursor();
    for (const auto& view : views_) {
        if (!view->mapped()) {
            continue;
        }
        view->refit();
        const wlr_box frame = view->frame();
        if (!wlr_output_layout_intersects(output_layout, nullptr, &frame) && !wlr_box_empty(&area)) {
            view->move_to(area.x + (view->content().x - frame.x),
                          area.y + (view->content().y - frame.y));
        }
    }
}

void Server::update_layer_focus() {
    // The topmost mapped top/overlay surface asking for exclusive keyboard
    // input (lock screens, launchers) takes it.
    LayerSurface* exclusive = nullptr;
    for (const auto layer : {ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, ZWLR_LAYER_SHELL_V1_LAYER_TOP}) {
        for (const auto& output : outputs_) {
            for (const auto& surface : output->layer_surfaces()) {
                if (exclusive == nullptr && surface->mapped() && surface->layer() == layer &&
                    surface->keyboard() == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
                    exclusive = surface.get();
                }
            }
        }
    }
    if (exclusive != exclusive_layer_) {
        exclusive_layer_ = exclusive;
        restore_keyboard_focus();
    }
}

void Server::handle_layer_mapped(LayerSurface& /*surface*/) {
    update_layer_focus();
    schedule_pointer_refocus();
}

void Server::handle_layer_unmapped(LayerSurface& surface) {
    if (focused_layer_ == &surface) {
        focused_layer_ = nullptr;
    }
    update_layer_focus();
    restore_keyboard_focus();
    schedule_pointer_refocus();
}

void Server::handle_layer_changed(LayerSurface& surface) {
    if (focused_layer_ == &surface &&
        surface.keyboard() == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
        focused_layer_ = nullptr;
    }
    update_layer_focus();
}

void Server::handle_layer_destroyed(LayerSurface& surface) {
    if (focused_layer_ == &surface) {
        focused_layer_ = nullptr;
    }
    if (exclusive_layer_ == &surface) {
        exclusive_layer_ = nullptr;
    }
    restore_keyboard_focus();
}

// --- focus ---------------------------------------------------------------------------

void Server::set_keyboard_focus(wlr_surface* surface) {
    if (seat->keyboard_state.focused_surface == surface) {
        return;
    }
    if (surface == nullptr) {
        wlr_seat_keyboard_notify_clear_focus(seat);
        return;
    }
    if (wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat)) {
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes, keyboard->num_keycodes,
                                       &keyboard->modifiers);
    } else {
        wlr_seat_keyboard_notify_enter(seat, surface, nullptr, 0, nullptr);
    }
}

void Server::restore_keyboard_focus() {
    if (locked()) {
        set_keyboard_focus(session_lock_->focus_surface()); // nothing else while locked
    } else if (exclusive_layer_ != nullptr) {
        set_keyboard_focus(exclusive_layer_->wlr()->surface);
    } else if (focused_layer_ != nullptr) {
        set_keyboard_focus(focused_layer_->wlr()->surface);
    } else if (focused_view_ != nullptr && focused_view_->mapped() && !focused_view_->minimized()) {
        set_keyboard_focus(focused_view_->surface());
    } else {
        set_keyboard_focus(nullptr);
    }
}

void Server::focus_view(View* view) {
    if (view == nullptr || !view->mapped()) {
        return;
    }
    if (view->minimized()) {
        view->set_minimized(false);
    }
    if (focused_view_ != nullptr && focused_view_ != view) {
        focused_view_->set_activated(false);
    }
    focused_view_ = view;
    view->raise();
    std::erase(focus_order_, view);
    focus_order_.insert(focus_order_.begin(), view);
    view->set_activated(true);
    focused_layer_ = nullptr;
    restore_keyboard_focus();
    taskbar_.view_focused(*view);
}

void Server::focus_next_view() {
    // Alt+Tab: bring the least recently used window forward.
    if (focus_order_.size() >= 2) {
        focus_view(focus_order_.back());
    }
}

namespace {

View* first_visible(const std::vector<View*>& order) {
    auto it = std::ranges::find_if(order, [](const View* v) { return !v->minimized(); });
    return it != order.end() ? *it : nullptr;
}

} // namespace

// --- view lifecycle --------------------------------------------------------------------

void Server::handle_view_mapped(View& view) {
    taskbar_.add_view(view);
    focus_view(&view);
    schedule_pointer_refocus();
}

void Server::handle_view_unmapped(View& view) {
    schedule_pointer_refocus();
    taskbar_.remove_view(view);
    std::erase(focus_order_, &view);
    if (hovered_view_ == &view) {
        hovered_view_ = nullptr;
    }
    if (grab_.view == &view) {
        end_grab();
    }
    if (last_title_click_view_ == &view) {
        last_title_click_view_ = nullptr;
    }
    if (focused_view_ == &view) {
        focused_view_ = nullptr;
        if (View* next = first_visible(focus_order_)) {
            focus_view(next);
        } else {
            restore_keyboard_focus();
        }
    }
}

void Server::handle_view_minimized(View& view) {
    schedule_pointer_refocus();
    if (!view.minimized() || focused_view_ != &view) {
        taskbar_.update();
        return;
    }
    view.set_activated(false);
    focused_view_ = nullptr;
    // Keep it in focus_order_ (Alt+Tab can restore it), but behind the rest.
    std::erase(focus_order_, &view);
    focus_order_.push_back(&view);
    if (View* next = first_visible(focus_order_)) {
        focus_view(next);
    } else {
        restore_keyboard_focus();
    }
    taskbar_.update();
}

void Server::handle_view_title_changed(View& /*view*/) {
    taskbar_.update();
}

void Server::destroy_view(View& view) {
    if (view.mapped()) {
        handle_view_unmapped(view);
    }
    std::erase(focus_order_, &view);
    if (focused_view_ == &view) {
        focused_view_ = nullptr;
    }
    if (hovered_view_ == &view) {
        hovered_view_ = nullptr;
    }
    if (grab_.view == &view) {
        end_grab();
    }
    if (last_title_click_view_ == &view) {
        last_title_click_view_ = nullptr;
    }
    std::erase_if(views_, [&](const auto& ptr) { return ptr.get() == &view; });
}

} // namespace tetra
