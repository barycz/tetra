#pragma once

// Single entry point for wlroots in C++.
//
// wlroots headers are C and use a few constructs C++ rejects: C99 array
// parameters (`const float color[static 4]`) and struct fields named after C++
// keywords (`class`, `namespace`, `delete`). We neutralise those with macros
// while the wlroots headers are parsed. Everything those headers include from
// outside wlroots is included *first*, so the macros only ever touch wlroots'
// own declarations (include guards make the later includes no-ops).
//
// After this header, the renamed fields are reachable as `class_`,
// `namespace_` and `delete_`.

#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <sys/types.h>

#include <drm_fourcc.h>
#include <libinput.h>
#include <pixman.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>
#include <xkbcommon/xkbcommon.h>

#ifndef WLR_USE_UNSTABLE
#define WLR_USE_UNSTABLE
#endif

extern "C" {
// NOLINTBEGIN
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define static
#define class class_
#define namespace namespace_
#define delete delete_

#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/backend/libinput.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/xcursor.h>
#include <wlr/xwayland/server.h>
#include <wlr/xwayland/shell.h>
#include <wlr/xwayland/xwayland.h>

#undef delete
#undef namespace
#undef class
#undef static
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
// NOLINTEND
}

namespace tetra {

// Typed replacement for wl_container_of (whose C casts trip -Wold-style-cast):
//   auto* mode = container_of<wlr_output_mode>(link, offsetof(wlr_output_mode, link));
template <typename T, typename Member>
T* container_of(Member* member, std::size_t offset) {
    return reinterpret_cast<T*>(reinterpret_cast<char*>(member) - offset); // NOLINT
}

} // namespace tetra
