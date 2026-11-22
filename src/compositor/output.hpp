#pragma once

#include "listener.hpp"
#include "wlr.hpp"

namespace tetra {

class Server;

// A display. Adds itself to the output layout and scene, and owns the
// desktop background rectangle underneath it.
class Output {
public:
    Output(Server& server, wlr_output* wlr_output);
    ~Output();

    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    Output(Output&&) = delete;
    Output& operator=(Output&&) = delete;

    [[nodiscard]] wlr_output* wlr() const noexcept { return output_; }

private:
    void render_frame();
    void update_background();

    Server& server_;
    wlr_output* output_;
    wlr_scene_rect* background_ = nullptr;

    Listener frame_;
    Listener request_state_;
    Listener destroy_;
};

} // namespace tetra
