#pragma once

#include "listener.hpp"
#include "view.hpp"

namespace tetra {

// A native Wayland window (xdg_toplevel).
class XdgView final : public View {
public:
    XdgView(Server& server, wlr_xdg_toplevel* toplevel);
    ~XdgView() override;

    XdgView(const XdgView&) = delete;
    XdgView& operator=(const XdgView&) = delete;
    XdgView(XdgView&&) = delete;
    XdgView& operator=(XdgView&&) = delete;

    [[nodiscard]] std::string_view title() const override;
    [[nodiscard]] std::string_view app_id() const override;
    [[nodiscard]] wlr_surface* surface() const override { return toplevel_->base->surface; }
    [[nodiscard]] wlr_xdg_toplevel* toplevel() const noexcept { return toplevel_; }
    void close() override;

    // xdg-decoration negotiation; tetra always answers "server side".
    // Clients without it draw their own decorations.
    void set_xdg_decoration(wlr_xdg_toplevel_decoration_v1* decoration);

protected:
    void configure(const wlr_box& content) override;
    void send_activated(bool activated) override;
    void send_maximized(bool maximized) override;
    void send_fullscreen(bool fullscreen) override;
    [[nodiscard]] wlr_box surface_geometry() const override;
    [[nodiscard]] bool is_main_window() const override { return toplevel_->parent == nullptr; }
    [[nodiscard]] View* parent_view() const override;

private:
    void apply_decoration_mode();

    wlr_xdg_toplevel* toplevel_;
    wlr_xdg_toplevel_decoration_v1* xdg_decoration_ = nullptr;
    PopupParent popup_parent_;

    Listener map_;
    Listener unmap_;
    Listener commit_;
    Listener destroy_;
    Listener request_move_;
    Listener request_resize_;
    Listener request_maximize_;
    Listener request_fullscreen_;
    Listener request_minimize_;
    Listener set_parent_;
    Listener set_title_;
    Listener set_app_id_;
    Listener decoration_destroy_;
    Listener decoration_request_mode_;
};

} // namespace tetra
