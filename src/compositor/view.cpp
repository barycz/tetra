#include "view.hpp"

#include "decoration.hpp"
#include "output.hpp"
#include "server.hpp"

#include <algorithm>

namespace tetra {

View::View(Server& server)
    : server_(server), tree_(wlr_scene_tree_create(server.layers.windows)),
      descriptor_{SceneDescriptor::Role::View, this} {
    tree_->node.data = &descriptor_;
    wlr_scene_node_set_enabled(&tree_->node, false);
}

View::~View() {
    decoration_.reset();
    wlr_scene_node_destroy(&tree_->node);
}

wlr_box View::frame() const {
    if (!decoration_) {
        return content_;
    }
    const auto e = decoration_->extents();
    return {.x = content_.x - e.left,
            .y = content_.y - e.top,
            .width = content_.width + e.left + e.right,
            .height = content_.height + e.top + e.bottom};
}

wlr_output* View::output() const {
    const double cx = content_.x + (content_.width / 2.0);
    const double cy = content_.y + (content_.height / 2.0);
    if (wlr_output* output = wlr_output_layout_output_at(server_.output_layout, cx, cy)) {
        return output;
    }
    double nx = 0;
    double ny = 0;
    wlr_output_layout_closest_point(server_.output_layout, nullptr, cx, cy, &nx, &ny);
    return wlr_output_layout_output_at(server_.output_layout, nx, ny);
}

wlr_box View::usable_area() const {
    if (const Output* out = server_.find_output(output())) {
        return out->usable_area();
    }
    return {};
}

wlr_box View::full_area() const {
    wlr_box box{};
    if (wlr_output* out = output()) {
        wlr_output_layout_get_box(server_.output_layout, out, &box);
    }
    return box;
}

void View::attach_surface_tree(wlr_scene_tree* surface_tree) {
    surface_tree_ = surface_tree;
    sync_surface_offset();
}

void View::detach_surface_tree() {
    if (surface_tree_ != nullptr) {
        wlr_scene_node_destroy(&surface_tree_->node);
        surface_tree_ = nullptr;
    }
}

void View::sync_surface_offset() {
    if (surface_tree_ != nullptr) {
        const wlr_box geo = surface_geometry();
        wlr_scene_node_set_position(&surface_tree_->node, -geo.x, -geo.y);
    }
}

void View::set_decorated(bool decorated) {
    if (decorated == (decoration_ != nullptr)) {
        return;
    }
    if (decorated) {
        decoration_ = std::make_unique<Decoration>(server_, *this);
        decoration_->update();
    } else {
        decoration_.reset();
    }
}

void View::update_decoration() {
    if (decoration_) {
        decoration_->update();
    }
}

void View::handle_map(bool want_maximized, bool want_fullscreen, bool place) {
    const wlr_box geo = surface_geometry();
    content_.width = geo.width;
    content_.height = geo.height;
    sync_surface_offset();
    if (!placed_) {
        if (place) {
            place_initially();
        }
        placed_ = true;
    }
    send_position();
    mapped_ = true;
    wlr_scene_node_set_enabled(&tree_->node, true);
    update_decoration();

    if (want_fullscreen) {
        set_fullscreen(true);
    } else if (want_maximized) {
        set_maximized(true);
    }
    server_.handle_view_mapped(*this);
}

void View::handle_unmap() {
    mapped_ = false;
    wlr_scene_node_set_enabled(&tree_->node, false);
    server_.handle_view_unmapped(*this);
}

void View::handle_commit() {
    const wlr_box geo = surface_geometry();
    const bool resized = geo.width != content_.width || geo.height != content_.height;
    content_.width = geo.width;
    content_.height = geo.height;
    sync_surface_offset();
    if (resized) {
        update_decoration();
    }
}

void View::handle_title_changed() {
    update_decoration();
    server_.handle_view_title_changed(*this);
}

void View::place_initially() {
    const wlr_box area = server_.usable_area_at_cursor();
    const wlr_box outer = frame();
    const int dx = content_.x - outer.x; // decoration extents
    const int dy = content_.y - outer.y;
    const int x = area.x + std::max(0, (area.width - outer.width) / 2);
    const int y = area.y + std::max(0, (area.height - outer.height) / 2);
    move_to(x + dx, y + dy);
}

void View::set_activated(bool activated) {
    if (activated_ == activated) {
        return;
    }
    activated_ = activated;
    send_activated(activated);
    update_decoration();
}

void View::raise() {
    wlr_scene_node_raise_to_top(&tree_->node);
}

void View::move_to(int x, int y) {
    content_.x = x;
    content_.y = y;
    wlr_scene_node_set_position(&tree_->node, x, y);
    if (mapped_) {
        send_position();
    }
}

wlr_box View::maximized_content() const {
    wlr_box area = usable_area();
    if (decoration_) {
        // Maximized windows keep their title bar but drop borders.
        const int title = server_.config().windows.titlebar_height;
        area.y += title;
        area.height -= title;
    }
    return area;
}

void View::set_maximized(bool maximized) {
    if (state_ == State::Fullscreen) {
        return;
    }
    if (maximized == (state_ == State::Maximized)) {
        send_maximized(maximized); // the client still expects a configure
        return;
    }
    if (maximized) {
        saved_ = content_;
        state_ = State::Maximized;
        const wlr_box box = maximized_content();
        move_to(box.x, box.y);
        send_maximized(true);
        configure(box);
    } else {
        state_ = State::Normal;
        move_to(saved_.x, saved_.y);
        send_maximized(false);
        configure(saved_);
    }
    update_decoration();
}

void View::set_fullscreen(bool fullscreen) {
    if (fullscreen == (state_ == State::Fullscreen)) {
        send_fullscreen(fullscreen);
        return;
    }
    if (fullscreen) {
        if (state_ == State::Normal) {
            saved_ = content_;
        }
        state_ = State::Fullscreen;
        wlr_scene_node_reparent(&tree_->node, server_.layers.fullscreen);
        if (decoration_) {
            decoration_->set_visible(false);
        }
        const wlr_box box = full_area();
        move_to(box.x, box.y);
        send_fullscreen(true);
        configure(box);
    } else {
        state_ = State::Normal;
        wlr_scene_node_reparent(&tree_->node, server_.layers.windows);
        if (decoration_) {
            decoration_->set_visible(true);
        }
        move_to(saved_.x, saved_.y);
        send_fullscreen(false);
        configure(saved_);
        update_decoration();
    }
}

void View::set_minimized(bool minimized) {
    if (minimized_ == minimized) {
        return;
    }
    minimized_ = minimized;
    wlr_scene_node_set_enabled(&tree_->node, mapped_ && !minimized);
    send_minimized(minimized);
    server_.handle_view_minimized(*this);
}

void View::refit() {
    if (state_ == State::Maximized) {
        const wlr_box box = maximized_content();
        move_to(box.x, box.y);
        configure(box);
    } else if (state_ == State::Fullscreen) {
        const wlr_box box = full_area();
        move_to(box.x, box.y);
        configure(box);
    } else {
        // The display got smaller (resolution, scale, a bar): keep the
        // window on it, smaller if it no longer fits.
        const wlr_box area = usable_area();
        if (wlr_box_empty(&area)) {
            return;
        }
        wlr_box outer = frame();
        const int left = content_.x - outer.x; // decoration extents
        const int top = content_.y - outer.y;
        const int extra_width = outer.width - content_.width;
        const int extra_height = outer.height - content_.height;
        const int width = std::min(content_.width, area.width - extra_width);
        const int height = std::min(content_.height, area.height - extra_height);
        if (width != content_.width || height != content_.height) {
            request_size(width, height);
            outer.width = width + extra_width;
            outer.height = height + extra_height;
        }
        const int x = std::clamp(outer.x, area.x, area.x + area.width - outer.width);
        const int y = std::clamp(outer.y, area.y, area.y + area.height - outer.height);
        if (x != outer.x || y != outer.y) {
            move_to(x + left, y + top);
        }
    }
}

} // namespace tetra
