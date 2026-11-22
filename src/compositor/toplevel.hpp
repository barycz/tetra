#pragma once

#include "listener.hpp"
#include "wlr.hpp"

namespace tetra {

class Server;

// An application window (xdg_toplevel). Its scene tree is owned by wlroots and
// destroyed together with the xdg surface.
class Toplevel {
public:
    Toplevel(Server& server, wlr_xdg_toplevel* xdg_toplevel);

    Toplevel(const Toplevel&) = delete;
    Toplevel& operator=(const Toplevel&) = delete;
    Toplevel(Toplevel&&) = delete;
    Toplevel& operator=(Toplevel&&) = delete;
    ~Toplevel() = default;

    [[nodiscard]] wlr_xdg_toplevel* xdg() const noexcept { return xdg_; }
    [[nodiscard]] wlr_surface* surface() const noexcept { return xdg_->base->surface; }
    // Window geometry within the surface (excludes client-side shadows).
    [[nodiscard]] const wlr_box& geometry() const noexcept { return xdg_->base->geometry; }
    [[nodiscard]] int x() const noexcept { return tree_->node.x; }
    [[nodiscard]] int y() const noexcept { return tree_->node.y; }

    void set_position(int x, int y);
    void set_activated(bool activated);
    void raise();

private:
    void handle_map();
    void center_on_active_output();

    Server& server_;
    wlr_xdg_toplevel* xdg_;
    wlr_scene_tree* tree_;
    bool placed_ = false;

    Listener map_;
    Listener unmap_;
    Listener commit_;
    Listener destroy_;
    Listener request_move_;
    Listener request_resize_;
    Listener request_maximize_;
    Listener request_fullscreen_;
};

} // namespace tetra
