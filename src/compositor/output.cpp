#include "output.hpp"

#include "server.hpp"

#include <ctime>

namespace tetra {

namespace {

// Placeholder desktop colour until theming lands.
constexpr float kBackgroundColor[4] = {0.106F, 0.114F, 0.133F, 1.0F};

} // namespace

Output::Output(Server& server, wlr_output* wlr_output) : server_(server), output_(wlr_output) {
    frame_.connect(output_->events.frame, [this] { render_frame(); });

    request_state_.connect<wlr_output_event_request_state>(
        output_->events.request_state, [this](auto* event) {
            // Nested backends ask for a new mode when their window is resized.
            wlr_output_commit_state(output_, event->state);
            update_background();
        });

    destroy_.connect(output_->events.destroy, [this] { server_.destroy_output(*this); });

    // Left-to-right auto arrangement until output configuration lands.
    wlr_output_layout_output* layout_output = wlr_output_layout_add_auto(server_.output_layout, output_);
    wlr_scene_output* scene_output = wlr_scene_output_create(server_.scene, output_);
    wlr_scene_output_layout_add_output(server_.scene_layout, layout_output, scene_output);

    background_ = wlr_scene_rect_create(server_.layers.background, 0, 0, kBackgroundColor);
    update_background();
}

Output::~Output() {
    if (background_ != nullptr) {
        wlr_scene_node_destroy(&background_->node);
    }
}

void Output::render_frame() {
    wlr_scene_output* scene_output = wlr_scene_get_scene_output(server_.scene, output_);
    if (scene_output == nullptr) {
        return;
    }
    wlr_scene_output_commit(scene_output, nullptr);

    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

void Output::update_background() {
    wlr_box box{};
    wlr_output_layout_get_box(server_.output_layout, output_, &box);
    wlr_scene_node_set_position(&background_->node, box.x, box.y);
    wlr_scene_rect_set_size(background_, box.width, box.height);
}

} // namespace tetra
