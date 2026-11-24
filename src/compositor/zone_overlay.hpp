#pragma once

#include "wlr.hpp"
#include "zones.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace tetra {

class Server;

// The snap zones of one display, drawn over the windows while a window is
// dragged with Shift held: every zone outlined and numbered, the zones the
// window would drop into filled with the accent colour.
class ZoneOverlay {
public:
    ZoneOverlay(Server& server, wlr_scene_tree* parent);
    ~ZoneOverlay();

    ZoneOverlay(const ZoneOverlay&) = delete;
    ZoneOverlay& operator=(const ZoneOverlay&) = delete;
    ZoneOverlay(ZoneOverlay&&) = delete;
    ZoneOverlay& operator=(ZoneOverlay&&) = delete;

    // `zones` in layout coordinates on `output`. Redraws only on change.
    void show(wlr_output* output, std::span<const zones::Box> zones, std::span<const std::size_t> picked);
    void hide();
    [[nodiscard]] bool visible() const noexcept { return output_ != nullptr; }

private:
    Server& server_;
    wlr_scene_buffer* buffer_;
    wlr_output* output_ = nullptr;
    std::vector<zones::Box> zones_;
    std::vector<std::size_t> picked_;
};

} // namespace tetra
