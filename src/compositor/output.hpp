#pragma once

#include "config.hpp"
#include "listener.hpp"
#include "wlr.hpp"

#include <chrono>
#include <memory>
#include <vector>

namespace tetra {

class LayerSurface;
class Server;

// A display: mode/scale/position from the config, the desktop background,
// and the layer-shell surfaces (bars, wallpapers, ...) placed on it.
class Output {
public:
    Output(Server& server, wlr_output* wlr_output);
    ~Output();

    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    Output(Output&&) = delete;
    Output& operator=(Output&&) = delete;

    [[nodiscard]] wlr_output* wlr() const noexcept { return output_; }
    // Full area in layout coordinates.
    [[nodiscard]] wlr_box box() const;
    // Area left for windows after exclusive zones (bars), layout coordinates.
    [[nodiscard]] const wlr_box& usable_area() const noexcept { return usable_area_; }
    [[nodiscard]] const std::vector<std::unique_ptr<LayerSurface>>& layer_surfaces() const noexcept {
        return layer_surfaces_;
    }

    // Applies mode, scale, position and enablement. `config` may be null.
    void apply_config(const config::Output* config);
    void update_background();
    void arrange_layers();

    void add_layer_surface(wlr_layer_surface_v1* layer_surface);
    void remove_layer_surface(LayerSurface& surface);

private:
    void render_frame();
    void ensure_scene_output();
    bool set_mode(wlr_output_state& state, const config::OutputMode& mode);

    Server& server_;
    wlr_output* output_;
    wlr_scene_rect* background_ = nullptr;
    wlr_box usable_area_{};
    std::vector<std::unique_ptr<LayerSurface>> layer_surfaces_;
    std::chrono::steady_clock::time_point last_slow_frame_log_;

    Listener frame_;
    Listener request_state_;
    Listener destroy_;
};

} // namespace tetra
