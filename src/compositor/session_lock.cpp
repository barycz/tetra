#include "session_lock.hpp"

#include "log.hpp"
#include "server.hpp"

#include <algorithm>
#include <array>

namespace tetra {

namespace {

// Big enough for any layout; the scene clips it to each output.
constexpr int kCoverOrigin = -(1 << 15);
constexpr int kCoverSize = 1 << 16;
constexpr std::array<float, 4> kCoverColor = {0, 0, 0, 1};

} // namespace

SessionLock::SessionLock(Server& server) : server_(server) {
    manager_ = wlr_session_lock_manager_v1_create(server_.display);
    tree_ = wlr_scene_tree_create(&server_.scene->tree); // created last: on top
    cover_ = wlr_scene_rect_create(tree_, kCoverSize, kCoverSize, kCoverColor.data());
    wlr_scene_node_set_position(&cover_->node, kCoverOrigin, kCoverOrigin);
    set_covered(false);

    new_lock_.connect<wlr_session_lock_v1>(
        manager_->events.new_lock, [this](wlr_session_lock_v1* lock) { handle_new_lock(lock); });
}

SessionLock::~SessionLock() {
    surfaces_.clear();
    new_lock_.disconnect();
    new_surface_.disconnect();
    unlock_.disconnect();
    lock_destroy_.disconnect();
}

void SessionLock::set_covered(bool covered) {
    wlr_scene_node_set_enabled(&tree_->node, covered);
}

void SessionLock::handle_new_lock(wlr_session_lock_v1* lock) {
    if (lock_ != nullptr) {
        log::info("session lock: already locked; refusing a second lock client");
        wlr_session_lock_v1_destroy(lock); // sends "finished"
        return;
    }
    const bool was_covered = state_ == State::Abandoned;
    lock_ = lock;
    new_surface_.connect<wlr_session_lock_surface_v1>(
        lock->events.new_surface,
        [this](wlr_session_lock_surface_v1* surface) { handle_new_surface(surface); });
    unlock_.connect(lock->events.unlock, [this] { handle_unlock(); });
    lock_destroy_.connect(lock->events.destroy, [this] { handle_lock_destroyed(); });

    set_covered(true);
    state_ = State::Locking;
    awaiting_frame_.clear();
    if (!was_covered) {
        // Each output must show the cover before the client may consider
        // the session locked (and e.g. let the system suspend).
        wl_list* outputs = &server_.output_layout->outputs;
        for (wl_list* link = outputs->next; link != outputs; link = link->next) {
            auto* l_output = container_of<wlr_output_layout_output>(
                link, offsetof(wlr_output_layout_output, link));
            if (l_output->output->enabled) {
                awaiting_frame_.push_back(l_output->output);
            }
        }
    }
    log::info("session locked");
    server_.handle_lock_changed();
    send_locked_if_ready();
}

void SessionLock::send_locked_if_ready() {
    if (state_ == State::Locking && awaiting_frame_.empty() && lock_ != nullptr) {
        wlr_session_lock_v1_send_locked(lock_);
        state_ = State::Locked;
    }
}

void SessionLock::handle_output_frame(wlr_output* output) {
    if (state_ != State::Locking) {
        return;
    }
    std::erase(awaiting_frame_, output);
    send_locked_if_ready();
}

void SessionLock::handle_output_destroyed(wlr_output* output) {
    std::erase(awaiting_frame_, output);
    send_locked_if_ready();
}

void SessionLock::handle_new_surface(wlr_session_lock_surface_v1* lock_surface) {
    auto surface = std::make_unique<Surface>();
    surface->lock_surface = lock_surface;
    surface->tree = wlr_scene_subsurface_tree_create(tree_, lock_surface->surface);
    Surface* raw = surface.get();
    surface->map.connect(lock_surface->surface->events.map, [this, raw] {
        if (focused_ == nullptr) {
            focused_ = raw->lock_surface->surface;
        } else {
            // Prefer the surface on the output under the cursor.
            wlr_output* under = wlr_output_layout_output_at(server_.output_layout,
                                                            server_.cursor->x, server_.cursor->y);
            if (raw->lock_surface->output == under) {
                focused_ = raw->lock_surface->surface;
            }
        }
        server_.handle_lock_changed();
    });
    surface->destroy.connect(lock_surface->events.destroy, [this, raw] {
        // This closure lives in *raw: nothing may touch its captures after
        // remove_surface() frees it.
        remove_surface(raw);
    });
    configure(*surface);
    surfaces_.push_back(std::move(surface));
}

void SessionLock::remove_surface(Surface* surface) {
    if (focused_ == surface->lock_surface->surface) {
        focused_ = nullptr;
    }
    wlr_scene_node_destroy(&surface->tree->node);
    std::erase_if(surfaces_, [surface](const auto& s) { return s.get() == surface; });
    if (focused_ == nullptr && !surfaces_.empty()) {
        focused_ = surfaces_.front()->lock_surface->surface;
    }
    server_.handle_lock_changed();
}

void SessionLock::configure(Surface& surface) {
    wlr_output* output = surface.lock_surface->output;
    wlr_box box{};
    wlr_output_layout_get_box(server_.output_layout, output, &box);
    if (wlr_box_empty(&box)) {
        return; // disabled output: nothing to show there
    }
    wlr_scene_node_set_position(&surface.tree->node, box.x, box.y);
    wlr_session_lock_surface_v1_configure(surface.lock_surface, static_cast<uint32_t>(box.width),
                                          static_cast<uint32_t>(box.height));
}

void SessionLock::handle_layout_changed() {
    for (const auto& surface : surfaces_) {
        configure(*surface);
    }
}

wlr_surface* SessionLock::focus_surface() const {
    if (focused_ != nullptr && focused_->mapped) {
        return focused_;
    }
    for (const auto& surface : surfaces_) {
        if (surface->lock_surface->surface->mapped) {
            return surface->lock_surface->surface;
        }
    }
    return nullptr;
}

void SessionLock::focus(wlr_surface* surface) {
    for (const auto& s : surfaces_) {
        if (s->lock_surface->surface == surface) {
            focused_ = surface;
            server_.handle_lock_changed();
            return;
        }
    }
}

void SessionLock::handle_unlock() {
    log::info("session unlocked");
    state_ = State::Unlocked;
    awaiting_frame_.clear();
    set_covered(false);
    // The lock object (and its surfaces) are destroyed right after.
}

void SessionLock::handle_lock_destroyed() {
    new_surface_.disconnect();
    unlock_.disconnect();
    lock_destroy_.disconnect();
    lock_ = nullptr;
    if (state_ != State::Unlocked) {
        // The client went away (crashed, killed) without unlocking: the
        // session stays covered until a new lock client takes over.
        log::error("session lock client died; staying locked");
        state_ = State::Abandoned;
        awaiting_frame_.clear();
        server_.request_lock();
    }
    server_.handle_lock_changed();
}

} // namespace tetra
