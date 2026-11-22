#pragma once

#include "scene.hpp"
#include "wlr.hpp"

#include <array>

namespace tetra {

class Server;
class View;

// Server-side window decoration: title bar with minimize / maximize / close
// buttons, a thin border, and an invisible margin for resizing.
//
// All coordinates are relative to the view's content origin, so the title
// bar occupies negative y.
class Decoration {
public:
    enum class Part { None, Titlebar, Minimize, Maximize, Close, Edge };
    struct Hit {
        Part part = Part::None;
        uint32_t edges = WLR_EDGE_NONE; // for Part::Edge
    };
    struct Extents {
        int top = 0;
        int left = 0;
        int right = 0;
        int bottom = 0;
    };

    Decoration(Server& server, View& view);
    ~Decoration();

    Decoration(const Decoration&) = delete;
    Decoration& operator=(const Decoration&) = delete;
    Decoration(Decoration&&) = delete;
    Decoration& operator=(Decoration&&) = delete;

    [[nodiscard]] Extents extents() const;
    [[nodiscard]] Hit hit_test(double x, double y) const;
    [[nodiscard]] static const char* cursor_for(const Hit& hit);

    // Re-layout and re-render from the view's current state.
    void update();
    void set_hover(Part part);
    void set_visible(bool visible);

private:
    void render_titlebar(int width, int height, float scale);
    [[nodiscard]] int border() const;
    [[nodiscard]] float render_scale() const;

    Server& server_;
    View& view_;
    SceneDescriptor descriptor_;
    wlr_scene_tree* tree_;
    wlr_scene_rect* resize_margin_;
    std::array<wlr_scene_rect*, 4> borders_{}; // top, bottom, left, right
    wlr_scene_buffer* titlebar_;
    Part hover_ = Part::None;
    bool visible_ = true;
};

} // namespace tetra
