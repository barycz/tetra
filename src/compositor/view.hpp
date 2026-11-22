#pragma once

#include "scene.hpp"
#include "wlr.hpp"

#include <memory>
#include <string_view>

namespace tetra {

class Decoration;
class Output;
class Server;

// A managed application window: native (XdgView) or X11 (XwaylandView).
//
// Coordinates: content() is the window's content area (the client's window
// geometry, excluding server-side decorations) in layout coordinates. The
// view's scene tree sits at the content origin; decorations extend into
// negative coordinates around it.
class View {
public:
    enum class State { Normal, Maximized, Fullscreen };

    explicit View(Server& server);
    virtual ~View();

    View(const View&) = delete;
    View& operator=(const View&) = delete;
    View(View&&) = delete;
    View& operator=(View&&) = delete;

    [[nodiscard]] virtual std::string_view title() const = 0;
    [[nodiscard]] virtual std::string_view app_id() const = 0;
    [[nodiscard]] virtual wlr_surface* surface() const = 0;
    virtual void close() = 0;

    [[nodiscard]] const wlr_box& content() const noexcept { return content_; }
    // Content plus decorations.
    [[nodiscard]] wlr_box frame() const;
    [[nodiscard]] bool mapped() const noexcept { return mapped_; }
    [[nodiscard]] bool minimized() const noexcept { return minimized_; }
    [[nodiscard]] bool activated() const noexcept { return activated_; }
    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] Decoration* decoration() const noexcept { return decoration_.get(); }
    [[nodiscard]] wlr_scene_tree* tree() const noexcept { return tree_; }
    // Output under the centre of the window (or the nearest one).
    [[nodiscard]] wlr_output* output() const;
    // That output's area left free by bars, in layout coordinates.
    [[nodiscard]] wlr_box usable_area() const;

    void set_activated(bool activated);
    void raise();
    void move_to(int x, int y);
    void set_maximized(bool maximized);
    void toggle_maximized() { set_maximized(state_ != State::Maximized); }
    void set_fullscreen(bool fullscreen);
    void set_minimized(bool minimized);
    // Ask the client for a new content size at the current position.
    void request_size(int width, int height) {
        configure({.x = content_.x, .y = content_.y, .width = width, .height = height});
    }
    // Re-fit a maximized/fullscreen view after its output's area changed.
    void refit();
    // Re-render decorations (theme, scale or config changed).
    void update_decoration();

    // Normal-state size to restore to; used when dragging a maximized window.
    [[nodiscard]] const wlr_box& saved_geometry() const noexcept { return saved_; }

protected:
    // Ask the client for a new content size (X11: and position).
    virtual void configure(const wlr_box& content) = 0;
    virtual void send_activated(bool activated) = 0;
    virtual void send_maximized(bool maximized) = 0;
    virtual void send_fullscreen(bool fullscreen) = 0;
    virtual void send_minimized(bool /*minimized*/) {}
    // Called after the view moved (X11 clients need to know their position).
    virtual void send_position() {}
    // Client window geometry within its surface (xdg window geometry).
    [[nodiscard]] virtual wlr_box surface_geometry() const = 0;

    // Subclasses attach their surface tree under tree() and call these.
    void attach_surface_tree(wlr_scene_tree* surface_tree);
    // Destroys the attached surface tree (X11 surfaces come and go with map).
    void detach_surface_tree();
    // `place`: centre the window on first map (false if it chose a position).
    void handle_map(bool want_maximized, bool want_fullscreen, bool place = true);
    void handle_unmap();
    void handle_commit();
    void set_decorated(bool decorated);
    void handle_title_changed();

    Server& server_;
    wlr_box content_{};

private:
    void place_initially();
    void sync_surface_offset();
    [[nodiscard]] wlr_box full_area() const;
    [[nodiscard]] wlr_box maximized_content() const;

    wlr_scene_tree* tree_;
    wlr_scene_tree* surface_tree_ = nullptr;
    std::unique_ptr<Decoration> decoration_;
    SceneDescriptor descriptor_;
    wlr_box saved_{};
    State state_ = State::Normal;
    bool mapped_ = false;
    bool minimized_ = false;
    bool activated_ = false;
    bool placed_ = false;
};

} // namespace tetra
