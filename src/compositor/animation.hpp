#pragma once

#include "wlr.hpp"

#include <array>
#include <chrono>
#include <memory>
#include <vector>

namespace tetra {

class Server;
class View;

// Window animations (theme.animations):
// - a window that opens, or comes back from the bar, fades in while
//   sliding up a few pixels;
// - a window that closes leaves a copy of its last picture (a "ghost")
//   that shrinks a little and fades out;
// - a minimised window's ghost shrinks towards the bar and fades.
// Ghosts are scene buffers sharing the window's last buffers, so they cost
// no copies; nothing about the real window changes but its opacity and
// position for a moment. Outputs advance the animations each frame and keep
// drawing while any runs.
class Animator {
public:
    explicit Animator(Server& server);
    ~Animator();

    Animator(const Animator&) = delete;
    Animator& operator=(const Animator&) = delete;
    Animator(Animator&&) = delete;
    Animator& operator=(Animator&&) = delete;

    // The view just mapped or was restored from the bar.
    void appear(View& view);
    // The view is about to unmap (its buffers are still in the scene).
    void close(View& view);
    // The view is about to be hidden on the bar.
    void minimize(View& view);
    // The view goes away: stop touching it.
    void forget(View& view);

    // Moves every animation to `now`; true while any still runs.
    bool tick(std::chrono::steady_clock::time_point now);
    [[nodiscard]] bool running() const noexcept { return !appearing_.empty() || !ghosts_.empty(); }

private:
    struct Appearing {
        View* view;
        std::chrono::steady_clock::time_point start;
    };
    struct GhostPart {
        wlr_scene_node* node;
        wlr_box box; // relative to the ghost's origin (the frame's top left)
        float opacity = 1;           // buffers
        std::array<float, 4> color{}; // rects (premultiplied)
    };
    struct Ghost {
        wlr_scene_tree* tree;
        wlr_box frame; // where the window was, layout coordinates
        std::vector<GhostPart> parts;
        std::chrono::steady_clock::time_point start;
        std::chrono::milliseconds duration;
        float end_scale;
        int end_dy; // how far it drops (towards the bar)
    };

    [[nodiscard]] bool enabled() const;
    std::unique_ptr<Ghost> make_ghost(View& view);
    void finish_appearing(Appearing& appearing);
    void apply_appearing(Appearing& appearing, float t);
    static void apply_ghost(Ghost& ghost, float t);
    void schedule_frames();

    Server& server_;
    std::vector<std::unique_ptr<Appearing>> appearing_;
    std::vector<std::unique_ptr<Ghost>> ghosts_;
};

} // namespace tetra
