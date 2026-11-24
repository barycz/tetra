#pragma once

#include "scene.hpp"
#include "wlr.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

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
    // After its output's area changed: re-fit a maximized/fullscreen view;
    // keep a normal one inside the area (shrinking it if needed).
    void refit();
    // Re-render decorations (theme, scale or config changed).
    void update_decoration();

    // Normal-state size to restore to; used when dragging a maximized window.
    [[nodiscard]] const wlr_box& saved_geometry() const noexcept { return saved_; }

    // Snap zones. Fits the frame into `zone` (layout coordinates); the size
    // it had before comes back when it is dragged out.
    void snap_to(const wlr_box& zone);
    // Leaves its zone: the size to go back to, if it was snapped.
    std::optional<std::pair<int, int>> unsnap();
    void forget_snap() { snap_.reset(); }
    [[nodiscard]] std::optional<wlr_box> snapped_zone() const {
        return snap_ ? std::optional(snap_->zone) : std::nullopt;
    }

    // Window memory (placement.cpp). Records where the window is now.
    void remember_place();
    // The displays changed: go where this window was with these displays.
    void restore_place();

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
    // A top-level window of its app (not a dialog): remembered by position.
    [[nodiscard]] virtual bool is_main_window() const = 0;

    // Window memory: claims the app's next free slot and picks the display
    // the window opens on. Returns the size to ask for, if one is known.
    // Done once; handle_map() does it if the subclass has not.
    std::optional<std::pair<int, int>> prepare_placement();

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
    // Places the window as remembered on `display`; false if nothing is.
    bool apply_remembered(const std::string& display, bool first_map);
    void release_slot();
    // Maximizes on `output`, returning to `normal` when unmaximized.
    void maximize_on(const Output& output, const wlr_box& normal);
    // `content` moved, and shrunk if needed, so the whole frame fits `area`.
    [[nodiscard]] wlr_box fit(wlr_box content, const wlr_box& area) const;
    [[nodiscard]] wlr_box maximized_box(const wlr_box& area) const;
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

    struct Snap {
        wlr_box zone;
        int width; // content size before snapping
        int height;
    };
    std::optional<Snap> snap_;

    // Window memory.
    bool memory_prepared_ = false;
    std::string memory_app_;
    std::optional<std::size_t> memory_slot_;
    std::optional<std::string> memory_display_; // where it opens
    std::optional<wlr_box> remembered_maximized_; // maximize on map, with this normal geometry
};

} // namespace tetra
