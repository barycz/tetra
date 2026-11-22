#pragma once

#include "wlr.hpp"

#include <cairo.h>

namespace tetra {

// A wlr_buffer backed by a CPU cairo image surface, for compositor-drawn UI
// (window decorations). Create, draw into surface(), then hand to a scene
// buffer and drop our reference:
//
//   auto* buffer = CairoBuffer::create(w, h);
//   cairo_t* cr = cairo_create(buffer->surface()); ... cairo_destroy(cr);
//   wlr_scene_buffer_set_buffer(node, buffer->base());
//   wlr_buffer_drop(buffer->base());
class CairoBuffer {
public:
    static CairoBuffer* create(int width, int height);

    [[nodiscard]] wlr_buffer* base() noexcept { return &base_; }
    [[nodiscard]] cairo_surface_t* surface() const noexcept { return surface_; }

private:
    CairoBuffer() = default;
    static const wlr_buffer_impl impl;
    static CairoBuffer* from(wlr_buffer* buffer);

    wlr_buffer base_{}; // must stay the first member
    cairo_surface_t* surface_ = nullptr;
};

} // namespace tetra
