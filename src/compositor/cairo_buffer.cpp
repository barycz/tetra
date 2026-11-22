#include "cairo_buffer.hpp"

#include <type_traits>

namespace tetra {

static_assert(std::is_standard_layout_v<CairoBuffer>);

const wlr_buffer_impl CairoBuffer::impl = {
    .destroy =
        [](wlr_buffer* buffer) {
            CairoBuffer* self = from(buffer);
            wlr_buffer_finish(buffer);
            cairo_surface_destroy(self->surface_);
            delete self; // NOLINT(cppcoreguidelines-owning-memory)
        },
    .get_dmabuf = nullptr,
    .get_shm = nullptr,
    .begin_data_ptr_access =
        [](wlr_buffer* buffer, uint32_t flags, void** data, uint32_t* format, size_t* stride) {
            if ((flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE) != 0) {
                return false;
            }
            CairoBuffer* self = from(buffer);
            *data = cairo_image_surface_get_data(self->surface_);
            *format = DRM_FORMAT_ARGB8888;
            *stride = static_cast<size_t>(cairo_image_surface_get_stride(self->surface_));
            return true;
        },
    .end_data_ptr_access = [](wlr_buffer* /*buffer*/) {},
};

CairoBuffer* CairoBuffer::from(wlr_buffer* buffer) {
    return reinterpret_cast<CairoBuffer*>(buffer); // NOLINT: base_ is the first member
}

CairoBuffer* CairoBuffer::create(int width, int height) {
    auto* self = new CairoBuffer(); // NOLINT(cppcoreguidelines-owning-memory): owned by wlroots
    wlr_buffer_init(&self->base_, &impl, width, height);
    self->surface_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    return self;
}

} // namespace tetra
