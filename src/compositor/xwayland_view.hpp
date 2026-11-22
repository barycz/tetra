#pragma once

#include "listener.hpp"
#include "view.hpp"

namespace tetra {

// A managed X11 window.
class XwaylandView final : public View {
public:
    XwaylandView(Server& server, wlr_xwayland_surface* xsurface);
    ~XwaylandView() override = default;

    XwaylandView(const XwaylandView&) = delete;
    XwaylandView& operator=(const XwaylandView&) = delete;
    XwaylandView(XwaylandView&&) = delete;
    XwaylandView& operator=(XwaylandView&&) = delete;

    [[nodiscard]] std::string_view title() const override;
    [[nodiscard]] std::string_view app_id() const override;
    [[nodiscard]] wlr_surface* surface() const override { return xsurface_->surface; }
    [[nodiscard]] wlr_xwayland_surface* xsurface() const noexcept { return xsurface_; }
    void close() override;

protected:
    void configure(const wlr_box& content) override;
    void send_activated(bool activated) override;
    void send_maximized(bool maximized) override;
    void send_fullscreen(bool fullscreen) override;
    void send_minimized(bool minimized) override;
    void send_position() override;
    [[nodiscard]] wlr_box surface_geometry() const override;

private:
    void handle_associate();
    void handle_dissociate();
    void update_decorated();

    wlr_xwayland_surface* xsurface_;

    Listener destroy_;
    Listener associate_;
    Listener dissociate_;
    Listener map_;
    Listener unmap_;
    Listener commit_;
    Listener request_configure_;
    Listener request_move_;
    Listener request_resize_;
    Listener request_maximize_;
    Listener request_fullscreen_;
    Listener request_minimize_;
    Listener request_activate_;
    Listener set_title_;
    Listener set_class_;
    Listener set_decorations_;
};

// An override-redirect X11 surface (menus, tooltips, drag icons). Not
// managed: shown where X11 puts it, no decorations. Deletes itself.
class XwaylandUnmanaged {
public:
    XwaylandUnmanaged(Server& server, wlr_xwayland_surface* xsurface);

    XwaylandUnmanaged(const XwaylandUnmanaged&) = delete;
    XwaylandUnmanaged& operator=(const XwaylandUnmanaged&) = delete;
    XwaylandUnmanaged(XwaylandUnmanaged&&) = delete;
    XwaylandUnmanaged& operator=(XwaylandUnmanaged&&) = delete;

private:
    ~XwaylandUnmanaged();

    Server& server_;
    wlr_xwayland_surface* xsurface_;
    wlr_scene_tree* tree_ = nullptr;

    Listener destroy_;
    Listener associate_;
    Listener dissociate_;
    Listener map_;
    Listener unmap_;
    Listener request_configure_;
    Listener set_geometry_;
};

} // namespace tetra
