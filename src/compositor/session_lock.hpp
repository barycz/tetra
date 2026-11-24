#pragma once

#include "listener.hpp"
#include "wlr.hpp"

#include <memory>
#include <vector>

namespace tetra {

class Server;

// ext-session-lock-v1: while a lock client (tetra-shell) holds the session,
// an opaque cover hides everything but its lock surfaces, which get all
// keyboard input. If the lock client dies without unlocking, the session
// stays covered ("abandoned") until a new lock client takes over; the shell
// is asked to lock again, also after it restarts.
class SessionLock {
public:
    explicit SessionLock(Server& server);
    ~SessionLock();

    SessionLock(const SessionLock&) = delete;
    SessionLock& operator=(const SessionLock&) = delete;
    SessionLock(SessionLock&&) = delete;
    SessionLock& operator=(SessionLock&&) = delete;

    // Covered: locking, locked or abandoned. Input then only reaches lock surfaces.
    [[nodiscard]] bool active() const noexcept { return state_ != State::Unlocked; }
    [[nodiscard]] bool abandoned() const noexcept { return state_ == State::Abandoned; }

    // The lock surface that should have keyboard focus, or null.
    [[nodiscard]] wlr_surface* focus_surface() const;
    // A click on a lock surface moves keyboard focus there (multi-monitor).
    void focus(wlr_surface* surface);

    // An output finished a frame; the client hears "locked" once every
    // output has shown the cover.
    void handle_output_frame(wlr_output* output);
    // Outputs were added, removed or changed mode: resize the lock surfaces.
    void handle_layout_changed();
    void handle_output_destroyed(wlr_output* output);

private:
    enum class State { Unlocked, Locking, Locked, Abandoned };

    struct Surface {
        wlr_session_lock_surface_v1* lock_surface = nullptr;
        wlr_scene_tree* tree = nullptr;
        Listener map;
        Listener destroy;
    };

    void handle_new_lock(wlr_session_lock_v1* lock);
    void handle_new_surface(wlr_session_lock_surface_v1* lock_surface);
    void remove_surface(Surface* surface);
    void handle_unlock();
    void handle_lock_destroyed();
    void configure(Surface& surface);
    void send_locked_if_ready();
    void set_covered(bool covered);

    Server& server_;
    wlr_session_lock_manager_v1* manager_ = nullptr;
    wlr_scene_tree* tree_ = nullptr; // above every other layer
    wlr_scene_rect* cover_ = nullptr;
    wlr_session_lock_v1* lock_ = nullptr;
    State state_ = State::Unlocked;
    std::vector<wlr_output*> awaiting_frame_; // outputs yet to show the cover
    std::vector<std::unique_ptr<Surface>> surfaces_;
    wlr_surface* focused_ = nullptr;

    Listener new_lock_;
    Listener new_surface_;
    Listener unlock_;
    Listener lock_destroy_;
};

} // namespace tetra
