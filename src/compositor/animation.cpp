#include "animation.hpp"

#include "decoration.hpp"
#include "output.hpp"
#include "server.hpp"
#include "view.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>

namespace tetra {

namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

constexpr milliseconds kAppear{180};
constexpr milliseconds kClose{150};
constexpr milliseconds kMinimize{220};
constexpr int kAppearSlide = 14; // pixels it rises while appearing

float ease_out(float t) {
    const float u = 1 - t;
    return 1 - (u * u * u);
}

float ease_in(float t) {
    return t * t;
}

// TETRA_SLOW_ANIMATIONS=N: N times slower, to look at them (and for tests).
float slow_factor() {
    static const float factor = [] {
        const char* value = std::getenv("TETRA_SLOW_ANIMATIONS"); // NOLINT(concurrency-mt-unsafe)
        const float f = value != nullptr ? std::strtof(value, nullptr) : 1.0F;
        return f >= 1 ? f : 1.0F;
    }();
    return factor;
}

float progress(Clock::time_point start, Clock::time_point now, milliseconds duration) {
    const auto elapsed = std::chrono::duration<float, std::milli>(now - start).count();
    return std::clamp(elapsed / (static_cast<float>(duration.count()) * slow_factor()), 0.0F, 1.0F);
}

wlr_scene_node* node_of(wl_list* link) {
    return reinterpret_cast<wlr_scene_node*>(reinterpret_cast<char*>(link) - offsetof(wlr_scene_node, link)); // NOLINT
}

// Calls `fn` for every node below `tree` (enabled or not).
template <typename Fn>
void for_each_node(wlr_scene_tree* tree, const Fn& fn) {
    for (wl_list* link = tree->children.next; link != &tree->children; link = link->next) {
        wlr_scene_node* node = node_of(link);
        fn(node);
        if (node->type == WLR_SCENE_NODE_TREE) {
            for_each_node(wlr_scene_tree_from_node(node), fn);
        }
    }
}

// The size a scene buffer is drawn at.
std::pair<int, int> drawn_size(const wlr_scene_buffer* buffer, const wlr_buffer* source) {
    if (buffer->dst_width > 0 && buffer->dst_height > 0) {
        return {buffer->dst_width, buffer->dst_height};
    }
    if (!wlr_fbox_empty(&buffer->src_box)) {
        return {static_cast<int>(buffer->src_box.width), static_cast<int>(buffer->src_box.height)};
    }
    const bool rotated = (buffer->transform % 2) != 0; // 90 and 270 degrees
    return rotated ? std::pair{source->height, source->width} : std::pair{source->width, source->height};
}

} // namespace

Animator::Animator(Server& server) : server_(server) {}

Animator::~Animator() {
    for (auto& appearing : appearing_) {
        finish_appearing(*appearing);
    }
    for (auto& ghost : ghosts_) {
        wlr_scene_node_destroy(&ghost->tree->node);
    }
}

bool Animator::enabled() const {
    return server_.config().theme.animations && !server_.terminating();
}

void Animator::schedule_frames() {
    for (const auto& output : server_.outputs()) {
        wlr_output_schedule_frame(output->wlr());
    }
}

void Animator::appear(View& view) {
    forget(view);
    if (!enabled() || server_.outputs().empty()) {
        return;
    }
    auto appearing = std::make_unique<Appearing>(Appearing{.view = &view, .start = Clock::now()});
    apply_appearing(*appearing, 0);
    appearing_.push_back(std::move(appearing));
    schedule_frames();
}

void Animator::close(View& view) {
    forget(view);
    if (!enabled() || view.minimized()) {
        return;
    }
    if (auto ghost = make_ghost(view)) {
        ghost->duration = kClose;
        ghost->end_scale = 0.92F;
        ghost->end_dy = 0;
        ghosts_.push_back(std::move(ghost));
        schedule_frames();
    }
}

void Animator::minimize(View& view) {
    forget(view);
    if (!enabled()) {
        return;
    }
    if (auto ghost = make_ghost(view)) {
        // Down towards the bar at the bottom of its display.
        const wlr_box area = view.usable_area();
        ghost->duration = kMinimize;
        ghost->end_scale = 0.6F;
        ghost->end_dy = std::max(0, (area.y + area.height) - (ghost->frame.y + (ghost->frame.height / 2)));
        ghosts_.push_back(std::move(ghost));
        schedule_frames();
    }
}

void Animator::forget(View& view) {
    std::erase_if(appearing_, [&](const auto& appearing) {
        if (appearing->view != &view) {
            return false;
        }
        finish_appearing(*appearing);
        return true;
    });
}

std::unique_ptr<Animator::Ghost> Animator::make_ghost(View& view) {
    if (!view.mapped()) {
        return nullptr;
    }
    auto ghost = std::make_unique<Ghost>();
    ghost->frame = view.frame();
    ghost->start = Clock::now();
    ghost->tree = wlr_scene_tree_create(view.tree()->node.parent); // windows or fullscreen layer
    wlr_scene_node_place_above(&ghost->tree->node, &view.tree()->node);
    wlr_scene_node_set_position(&ghost->tree->node, ghost->frame.x, ghost->frame.y);

    // Flatten the window's visible nodes into the ghost, each at its place
    // relative to the frame, so the whole picture can be scaled.
    auto clone = [&](auto& self, wlr_scene_node* node, int x, int y) -> void {
        if (!node->enabled) {
            return;
        }
        x += node->x;
        y += node->y;
        const int gx = x - ghost->frame.x;
        const int gy = y - ghost->frame.y;
        switch (node->type) {
        case WLR_SCENE_NODE_TREE: {
            wlr_scene_tree* tree = wlr_scene_tree_from_node(node);
            for (wl_list* link = tree->children.next; link != &tree->children; link = link->next) {
                self(self, node_of(link), x, y);
            }
            break;
        }
        case WLR_SCENE_NODE_RECT: {
            const wlr_scene_rect* rect = wlr_scene_rect_from_node(node);
            if (rect->color[3] <= 0 || rect->width <= 0 || rect->height <= 0) {
                return; // invisible (resize margins)
            }
            wlr_scene_rect* copy = wlr_scene_rect_create(ghost->tree, rect->width, rect->height, rect->color);
            wlr_scene_node_set_position(&copy->node, gx, gy);
            GhostPart part{.node = &copy->node, .box = {gx, gy, rect->width, rect->height}};
            std::copy(std::begin(rect->color), std::end(rect->color), part.color.begin());
            ghost->parts.push_back(part);
            break;
        }
        case WLR_SCENE_NODE_BUFFER: {
            wlr_scene_buffer* buffer = wlr_scene_buffer_from_node(node);
            wlr_buffer* source = buffer->buffer;
            if (source == nullptr && view.decoration() != nullptr && node == view.decoration()->titlebar_node()) {
                source = view.decoration()->title_buffer();
            }
            if (source == nullptr || buffer->opacity <= 0) {
                return;
            }
            const auto [w, h] = drawn_size(buffer, source);
            wlr_scene_buffer* copy = wlr_scene_buffer_create(ghost->tree, source);
            if (!wlr_fbox_empty(&buffer->src_box)) {
                wlr_scene_buffer_set_source_box(copy, &buffer->src_box);
            }
            wlr_scene_buffer_set_transform(copy, buffer->transform);
            wlr_scene_buffer_set_dest_size(copy, w, h);
            wlr_scene_buffer_set_opacity(copy, buffer->opacity);
            wlr_scene_node_set_position(&copy->node, gx, gy);
            ghost->parts.push_back({.node = &copy->node, .box = {gx, gy, w, h}, .opacity = buffer->opacity});
            break;
        }
        }
    };
    // The view's tree sits in the windows layer, at the content origin.
    clone(clone, &view.tree()->node, 0, 0);
    if (ghost->parts.empty()) {
        wlr_scene_node_destroy(&ghost->tree->node);
        return nullptr;
    }
    return ghost;
}

void Animator::apply_appearing(Appearing& appearing, float t) {
    View& view = *appearing.view;
    const float alpha = ease_out(t);
    for_each_node(view.tree(), [&](wlr_scene_node* node) {
        if (node->type == WLR_SCENE_NODE_BUFFER) {
            wlr_scene_buffer_set_opacity(wlr_scene_buffer_from_node(node), alpha);
        }
    });
    if (Decoration* decoration = view.decoration()) {
        decoration->set_opacity(alpha); // its borders
    }
    const int slide = static_cast<int>(std::lround((1 - alpha) * kAppearSlide));
    wlr_scene_node_set_position(&view.tree()->node, view.content().x, view.content().y + slide);
}

void Animator::finish_appearing(Appearing& appearing) {
    apply_appearing(appearing, 1);
}

void Animator::apply_ghost(Ghost& ghost, float t) {
    const float e = ease_in(t);
    const float scale = 1 + ((ghost.end_scale - 1) * e);
    const float alpha = 1 - e;
    const float cx = static_cast<float>(ghost.frame.width) / 2;
    const float cy = static_cast<float>(ghost.frame.height) / 2;
    wlr_scene_node_set_position(&ghost.tree->node, ghost.frame.x,
                                ghost.frame.y + static_cast<int>(std::lround(static_cast<float>(ghost.end_dy) * e)));
    for (const GhostPart& part : ghost.parts) {
        const auto x = static_cast<int>(std::lround(cx + ((static_cast<float>(part.box.x) - cx) * scale)));
        const auto y = static_cast<int>(std::lround(cy + ((static_cast<float>(part.box.y) - cy) * scale)));
        const int w = std::max(1, static_cast<int>(std::lround(static_cast<float>(part.box.width) * scale)));
        const int h = std::max(1, static_cast<int>(std::lround(static_cast<float>(part.box.height) * scale)));
        wlr_scene_node_set_position(part.node, x, y);
        if (part.node->type == WLR_SCENE_NODE_BUFFER) {
            wlr_scene_buffer* buffer = wlr_scene_buffer_from_node(part.node);
            wlr_scene_buffer_set_dest_size(buffer, w, h);
            wlr_scene_buffer_set_opacity(buffer, part.opacity * alpha);
        } else {
            wlr_scene_rect* rect = wlr_scene_rect_from_node(part.node);
            wlr_scene_rect_set_size(rect, w, h);
            std::array<float, 4> color = part.color;
            for (float& c : color) {
                c *= alpha; // premultiplied
            }
            wlr_scene_rect_set_color(rect, color.data());
        }
    }
}

bool Animator::tick(Clock::time_point now) {
    std::erase_if(appearing_, [&](const auto& appearing) {
        const float t = progress(appearing->start, now, kAppear);
        apply_appearing(*appearing, t);
        return t >= 1;
    });
    std::erase_if(ghosts_, [&](const auto& ghost) {
        const float t = progress(ghost->start, now, ghost->duration);
        if (t >= 1) {
            wlr_scene_node_destroy(&ghost->tree->node);
            return true;
        }
        apply_ghost(*ghost, t);
        return false;
    });
    return running();
}

} // namespace tetra
