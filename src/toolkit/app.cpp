#include "app.hpp"

#include "clipboard.hpp"
#include "keymap.hpp"
#include "protocols.hpp"
#include "window.hpp"

#include <EGL/eglext.h>
#include <fontconfig/fontconfig.h>
#include <imgui.h>
#include <linux/input-event-codes.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <print>
#include <string_view>

#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

namespace tetra::ui {

namespace {

std::filesystem::path config_path() {
    return config::active_file();
}

} // namespace

std::string find_font(const std::string& family) {
    FcInit();
    FcPattern* pattern = FcNameParse(reinterpret_cast<const FcChar8*>(family.c_str())); // NOLINT
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result = FcResultNoMatch;
    FcPattern* match = FcFontMatch(nullptr, pattern, &result);
    std::string path;
    FcChar8* file = nullptr;
    if (match != nullptr && FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch) {
        path = reinterpret_cast<const char*>(file); // NOLINT
    }
    if (match != nullptr) {
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pattern);
    return path;
}

namespace {

uint32_t cursor_shape(int imgui_cursor) {
    switch (imgui_cursor) {
    case ImGuiMouseCursor_TextInput:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT;
    case ImGuiMouseCursor_ResizeAll:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE;
    case ImGuiMouseCursor_ResizeNS:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE;
    case ImGuiMouseCursor_ResizeEW:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE;
    case ImGuiMouseCursor_ResizeNESW:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE;
    case ImGuiMouseCursor_ResizeNWSE:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE;
    case ImGuiMouseCursor_Hand:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;
    case ImGuiMouseCursor_Wait:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_WAIT;
    case ImGuiMouseCursor_Progress:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_PROGRESS;
    case ImGuiMouseCursor_NotAllowed:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED;
    default:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    }
}

template <typename T>
T* bind(wl_registry* registry, uint32_t name, const wl_interface* interface, uint32_t version) {
    return static_cast<T*>(wl_registry_bind(registry, name, interface, version));
}

} // namespace

// Static listener tables; friends of App so they can forward to its handlers.
struct Listeners {
    static const wl_registry_listener registry;
    static const xdg_wm_base_listener wm_base;
    static const wl_seat_listener seat;
    static const wl_pointer_listener pointer;
    static const wl_keyboard_listener keyboard;
    static const wl_output_listener output;
};

const wl_registry_listener Listeners::registry = {
    .global =
        [](void* data, wl_registry* registry, uint32_t name, const char* interface,
           uint32_t version) {
            static_cast<App*>(data)->handle_global(registry, name, interface, version);
        },
    .global_remove = [](void* data, wl_registry* /*registry*/,
                        uint32_t name) { static_cast<App*>(data)->handle_global_remove(name); },
};

const xdg_wm_base_listener Listeners::wm_base = {
    .ping = [](void* /*data*/, xdg_wm_base* base, uint32_t serial) { xdg_wm_base_pong(base, serial); },
};

const wl_seat_listener Listeners::seat = {
    .capabilities = [](void* data, wl_seat* /*seat*/,
                       uint32_t caps) { static_cast<App*>(data)->handle_seat_capabilities(caps); },
    .name = [](void* /*data*/, wl_seat* /*seat*/, const char* /*name*/) {},
};

const wl_pointer_listener Listeners::pointer = {
    .enter =
        [](void* data, wl_pointer* /*pointer*/, uint32_t serial, wl_surface* surface, wl_fixed_t x,
           wl_fixed_t y) {
            auto* app = static_cast<App*>(data);
            app->pointer_serial_ = serial;
            app->pointer_window_ = app->window_for(surface);
            app->cursor_ = -2; // the compositor forgets our cursor on enter
            app->set_cursor(ImGuiMouseCursor_Arrow);
            if (app->pointer_window_ != nullptr) {
                app->pointer_window_->handle_pointer_enter(wl_fixed_to_double(x),
                                                           wl_fixed_to_double(y));
            }
        },
    .leave =
        [](void* data, wl_pointer* /*pointer*/, uint32_t /*serial*/, wl_surface* /*surface*/) {
            auto* app = static_cast<App*>(data);
            if (app->pointer_window_ != nullptr) {
                app->pointer_window_->handle_pointer_leave();
            }
            app->pointer_window_ = nullptr;
        },
    .motion =
        [](void* data, wl_pointer* /*pointer*/, uint32_t /*time*/, wl_fixed_t x, wl_fixed_t y) {
            auto* app = static_cast<App*>(data);
            if (app->pointer_window_ != nullptr) {
                app->pointer_window_->handle_pointer_motion(wl_fixed_to_double(x),
                                                            wl_fixed_to_double(y));
            }
        },
    .button =
        [](void* data, wl_pointer* /*pointer*/, uint32_t serial, uint32_t /*time*/, uint32_t button,
           uint32_t state) {
            auto* app = static_cast<App*>(data);
            app->pointer_serial_ = serial;
            if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
                app->input_serial_ = serial;
            }
            if (app->pointer_window_ != nullptr) {
                app->pointer_window_->handle_pointer_button(button,
                                                            state == WL_POINTER_BUTTON_STATE_PRESSED);
            }
        },
    // Scrolling is gathered over a pointer frame: wheels send value120
    // (120 per notch) besides the value, touchpads only the value.
    .axis =
        [](void* data, wl_pointer* /*pointer*/, uint32_t /*time*/, uint32_t axis, wl_fixed_t value) {
            auto* app = static_cast<App*>(data);
            app->axis_value_.at(axis == WL_POINTER_AXIS_VERTICAL_SCROLL ? 1 : 0) += wl_fixed_to_double(value);
        },
    .frame =
        [](void* data, wl_pointer* /*pointer*/) {
            auto* app = static_cast<App*>(data);
            // Notches, positive = up/left (ImGui); Wayland: positive = down/right,
            // about 15 units per wheel notch.
            std::array<double, 2> notches{};
            for (std::size_t i = 0; i < 2; ++i) {
                notches.at(i) = app->axis_value120_.at(i) != 0 ? -app->axis_value120_.at(i) / 120.0
                                                               : -app->axis_value_.at(i) / 15.0;
            }
            app->axis_value_ = {};
            app->axis_value120_ = {};
            if (app->pointer_window_ != nullptr && (notches[0] != 0 || notches[1] != 0)) {
                app->pointer_window_->handle_pointer_axis(notches[0], notches[1]);
            }
        },
    .axis_source = [](void* /*data*/, wl_pointer* /*pointer*/, uint32_t /*source*/) {},
    .axis_stop = [](void* /*data*/, wl_pointer* /*pointer*/, uint32_t /*time*/, uint32_t /*axis*/) {},
    .axis_discrete = [](void* /*data*/, wl_pointer* /*pointer*/, uint32_t /*axis*/,
                        int32_t /*discrete*/) {},
    .axis_value120 =
        [](void* data, wl_pointer* /*pointer*/, uint32_t axis, int32_t value120) {
            auto* app = static_cast<App*>(data);
            app->axis_value120_.at(axis == WL_POINTER_AXIS_VERTICAL_SCROLL ? 1 : 0) += value120;
        },
    .axis_relative_direction = [](void* /*data*/, wl_pointer* /*pointer*/, uint32_t /*axis*/,
                                  uint32_t /*direction*/) {},
    .warp = [](void* /*data*/, wl_pointer* /*pointer*/, wl_fixed_t /*x*/, wl_fixed_t /*y*/) {},
};

const wl_keyboard_listener Listeners::keyboard = {
    .keymap = [](void* data, wl_keyboard* /*keyboard*/, uint32_t format, int32_t fd,
                 uint32_t size) { static_cast<App*>(data)->handle_keymap(format, fd, size); },
    .enter =
        [](void* data, wl_keyboard* /*keyboard*/, uint32_t serial, wl_surface* surface,
           wl_array* /*keys*/) {
            auto* app = static_cast<App*>(data);
            app->input_serial_ = serial;
            app->keyboard_window_ = app->window_for(surface);
            if (app->keyboard_window_ != nullptr) {
                app->keyboard_window_->handle_keyboard_focus(true);
            }
        },
    .leave =
        [](void* data, wl_keyboard* /*keyboard*/, uint32_t /*serial*/, wl_surface* /*surface*/) {
            auto* app = static_cast<App*>(data);
            app->stop_key_repeat();
            if (app->keyboard_window_ != nullptr) {
                app->keyboard_window_->handle_keyboard_focus(false);
            }
            app->keyboard_window_ = nullptr;
        },
    .key =
        [](void* data, wl_keyboard* /*keyboard*/, uint32_t serial, uint32_t /*time*/,
           uint32_t key, uint32_t state) {
            auto* app = static_cast<App*>(data);
            if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
                app->input_serial_ = serial;
            }
            app->handle_key(key, state == WL_KEYBOARD_KEY_STATE_PRESSED);
        },
    .modifiers =
        [](void* data, wl_keyboard* /*keyboard*/, uint32_t /*serial*/, uint32_t depressed,
           uint32_t latched, uint32_t locked, uint32_t group) {
            static_cast<App*>(data)->handle_modifiers(depressed, latched, locked, group);
        },
    .repeat_info =
        [](void* data, wl_keyboard* /*keyboard*/, int32_t rate, int32_t delay) {
            auto* app = static_cast<App*>(data);
            app->repeat_rate_ = rate;
            app->repeat_delay_ = delay;
        },
};

const wl_output_listener Listeners::output = {
    .geometry = [](void* /*data*/, wl_output* /*output*/, int32_t /*x*/, int32_t /*y*/,
                   int32_t /*physical_width*/, int32_t /*physical_height*/, int32_t /*subpixel*/,
                   const char* /*make*/, const char* /*model*/, int32_t /*transform*/) {},
    .mode =
        [](void* data, wl_output* /*output*/, uint32_t flags, int32_t width, int32_t height,
           int32_t /*refresh*/) {
            auto* out = static_cast<App::Output*>(data);
            if ((flags & WL_OUTPUT_MODE_CURRENT) != 0) {
                out->info.width = width;
                out->info.height = height;
            }
        },
    .done =
        [](void* data, wl_output* /*output*/) {
            auto* out = static_cast<App::Output*>(data);
            if (!out->info.done) {
                out->info.done = true;
                if (out->app->started_ && out->app->on_output_added) {
                    out->app->on_output_added(out->info);
                }
            }
        },
    .scale = [](void* data, wl_output* /*output*/,
                int32_t factor) { static_cast<App::Output*>(data)->info.scale = factor; },
    .name = [](void* data, wl_output* /*output*/,
               const char* name) { static_cast<App::Output*>(data)->info.name = name; },
    .description =
        [](void* data, wl_output* /*output*/, const char* description) {
            static_cast<App::Output*>(data)->info.description = description;
        },
};

// --- lifetime ------------------------------------------------------------------------

std::expected<std::unique_ptr<App>, std::string> App::create(std::string app_id) {
    std::unique_ptr<App> app{new App(std::move(app_id))};
    if (auto result = app->init(); !result) {
        return std::unexpected(std::move(result.error()));
    }
    return app;
}

App::App(std::string app_id) : app_id_(std::move(app_id)) {}

App::~App() {
    // Windows are owned by the application and must be gone by now.
    clipboard_.reset();
    if (data_device_manager_ != nullptr) {
        wl_data_device_manager_destroy(data_device_manager_);
    }
    if (primary_selection_manager_ != nullptr) {
        zwp_primary_selection_device_manager_v1_destroy(primary_selection_manager_);
    }
    if (cursor_shape_device_ != nullptr) {
        wp_cursor_shape_device_v1_destroy(cursor_shape_device_);
    }
    if (pointer_ != nullptr) {
        wl_pointer_release(pointer_);
    }
    if (keyboard_ != nullptr) {
        wl_keyboard_release(keyboard_);
    }
    if (xkb_state_ != nullptr) {
        xkb_state_unref(xkb_state_);
    }
    if (xkb_keymap_ != nullptr) {
        xkb_keymap_unref(xkb_keymap_);
    }
    if (xkb_context_ != nullptr) {
        xkb_context_unref(xkb_context_);
    }
    for (const auto& output : outputs_) {
        wl_output_release(output->info.output);
    }
    if (seat_ != nullptr) {
        wl_seat_release(seat_);
    }
    if (cursor_shape_manager_ != nullptr) {
        wp_cursor_shape_manager_v1_destroy(cursor_shape_manager_);
    }
    if (decoration_manager_ != nullptr) {
        zxdg_decoration_manager_v1_destroy(decoration_manager_);
    }
    if (layer_shell_ != nullptr) {
        zwlr_layer_shell_v1_destroy(layer_shell_);
    }
    if (wm_base_ != nullptr) {
        xdg_wm_base_destroy(wm_base_);
    }
    if (compositor_ != nullptr) {
        wl_compositor_destroy(compositor_);
    }
    if (registry_ != nullptr) {
        wl_registry_destroy(registry_);
    }
    if (egl_display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_context_ != EGL_NO_CONTEXT) {
            eglDestroyContext(egl_display_, egl_context_);
        }
        eglTerminate(egl_display_);
    }
    if (display_ != nullptr) {
        wl_display_disconnect(display_);
    }
    FcFini();
}

std::expected<void, std::string> App::init() {
    load_config(/*initial=*/true);
    xkb_context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    display_ = wl_display_connect(nullptr);
    if (display_ == nullptr) {
        return std::unexpected("cannot connect to the Wayland compositor");
    }
    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &Listeners::registry, this);
    wl_display_roundtrip(display_); // globals
    wl_display_roundtrip(display_); // output and seat properties
    if (compositor_ == nullptr) {
        return std::unexpected("compositor does not provide wl_compositor");
    }
    if (seat_ != nullptr) {
        clipboard_ = std::make_unique<Clipboard>(*this, seat_, data_device_manager_, primary_selection_manager_);
    }
    if (auto result = init_egl(); !result) {
        return result;
    }

    if (auto watcher = FileWatcher::create(config_path())) {
        config_watcher_ = std::move(*watcher);
        watch_fd(config_watcher_->fd(), [this] {
            if (!config_watcher_->changed()) {
                return;
            }
            const config::Config previous = config_;
            load_config(/*initial=*/false);
            if (config_ == previous) {
                return;
            }
            for (Window* window : windows_) {
                window->apply_theme();
            }
            if (on_config_changed) {
                on_config_changed();
            }
        });
    }
    return {};
}

std::expected<void, std::string> App::init_egl() {
    egl_display_ = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, display_, nullptr);
    if (egl_display_ == EGL_NO_DISPLAY || eglInitialize(egl_display_, nullptr, nullptr) == EGL_FALSE) {
        return std::unexpected("cannot initialise EGL");
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    const std::array<EGLint, 13> config_attribs = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE,        8, EGL_GREEN_SIZE,       8,
        EGL_BLUE_SIZE,    8,              EGL_ALPHA_SIZE,      8, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_NONE};
    EGLint count = 0;
    if (eglChooseConfig(egl_display_, config_attribs.data(), &egl_config_, 1, &count) == EGL_FALSE ||
        count == 0) {
        return std::unexpected("no suitable EGL config (need OpenGL ES 3 with alpha)");
    }
    const std::array<EGLint, 3> context_attribs = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
    egl_context_ = eglCreateContext(egl_display_, egl_config_, EGL_NO_CONTEXT, context_attribs.data());
    if (egl_context_ == EGL_NO_CONTEXT) {
        return std::unexpected("cannot create an OpenGL ES 3 context");
    }
    eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_context_);
    return {};
}

void App::load_config(bool initial) {
    auto loaded = config::load(config_path());
    if (loaded) {
        config_ = std::move(*loaded);
    } else {
        std::println(stderr, "{}: {}", app_id_, loaded.error());
        if (!initial) {
            return; // keep the previous configuration
        }
        config_ = {};
    }
    palette_ = theme::make_palette(config_.theme);
    font_path_ = find_font(config_.theme.font);
}

// --- globals -------------------------------------------------------------------------

void* App::bind_global(const wl_interface* interface, uint32_t max_version) {
    auto it = std::ranges::find_if(globals_, [&](const Global& g) { return g.interface == interface->name; });
    if (it == globals_.end()) {
        return nullptr;
    }
    return wl_registry_bind(registry_, it->name, interface, std::min(it->version, max_version));
}

void App::make_gl_current() const {
    eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_context_);
}

void App::handle_global(wl_registry* registry, uint32_t name, const char* interface,
                        uint32_t version) {
    const std::string_view iface{interface};
    globals_.push_back({name, std::string(iface), version});
    if (iface == wl_compositor_interface.name) {
        compositor_ = bind<wl_compositor>(registry, name, &wl_compositor_interface, std::min(version, 6U));
    } else if (iface == xdg_wm_base_interface.name) {
        wm_base_ = bind<xdg_wm_base>(registry, name, &xdg_wm_base_interface, std::min(version, 5U));
        xdg_wm_base_add_listener(wm_base_, &Listeners::wm_base, this);
    } else if (iface == zwlr_layer_shell_v1_interface.name) {
        layer_shell_ = bind<zwlr_layer_shell_v1>(registry, name, &zwlr_layer_shell_v1_interface,
                                                 std::min(version, 4U));
    } else if (iface == zxdg_decoration_manager_v1_interface.name) {
        decoration_manager_ = bind<zxdg_decoration_manager_v1>(
            registry, name, &zxdg_decoration_manager_v1_interface, 1);
    } else if (iface == wp_cursor_shape_manager_v1_interface.name) {
        cursor_shape_manager_ = bind<wp_cursor_shape_manager_v1>(
            registry, name, &wp_cursor_shape_manager_v1_interface, 1);
    } else if (iface == wl_data_device_manager_interface.name) {
        data_device_manager_ = bind<wl_data_device_manager>(registry, name, &wl_data_device_manager_interface,
                                                            std::min(version, 3U));
    } else if (iface == zwp_primary_selection_device_manager_v1_interface.name) {
        primary_selection_manager_ = bind<zwp_primary_selection_device_manager_v1>(
            registry, name, &zwp_primary_selection_device_manager_v1_interface, 1);
    } else if (iface == wl_seat_interface.name && seat_ == nullptr) {
        seat_ = bind<wl_seat>(registry, name, &wl_seat_interface, std::min(version, 8U));
        wl_seat_add_listener(seat_, &Listeners::seat, this);
    } else if (iface == wl_output_interface.name) {
        auto output = std::make_unique<Output>(Output{.app = this, .info = {}});
        output->info.global = name;
        output->info.output = bind<wl_output>(registry, name, &wl_output_interface, std::min(version, 4U));
        wl_output_add_listener(output->info.output, &Listeners::output, output.get());
        outputs_.push_back(std::move(output));
    }
}

void App::handle_global_remove(uint32_t name) {
    std::erase_if(globals_, [&](const Global& g) { return g.name == name; });
    auto it = std::ranges::find_if(outputs_, [&](const auto& o) { return o->info.global == name; });
    if (it == outputs_.end()) {
        return;
    }
    if ((*it)->info.done && started_ && on_output_removed) {
        on_output_removed((*it)->info);
    }
    wl_output_release((*it)->info.output);
    outputs_.erase(it);
}

// --- input ---------------------------------------------------------------------------

void App::handle_seat_capabilities(uint32_t caps) {
    const bool has_pointer = (caps & WL_SEAT_CAPABILITY_POINTER) != 0;
    if (has_pointer && pointer_ == nullptr) {
        pointer_ = wl_seat_get_pointer(seat_);
        wl_pointer_add_listener(pointer_, &Listeners::pointer, this);
        if (cursor_shape_manager_ != nullptr) {
            cursor_shape_device_ = wp_cursor_shape_manager_v1_get_pointer(cursor_shape_manager_, pointer_);
        }
    } else if (!has_pointer && pointer_ != nullptr) {
        if (cursor_shape_device_ != nullptr) {
            wp_cursor_shape_device_v1_destroy(cursor_shape_device_);
            cursor_shape_device_ = nullptr;
        }
        wl_pointer_release(pointer_);
        pointer_ = nullptr;
        pointer_window_ = nullptr;
    }

    const bool has_keyboard = (caps & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
    if (has_keyboard && keyboard_ == nullptr) {
        keyboard_ = wl_seat_get_keyboard(seat_);
        wl_keyboard_add_listener(keyboard_, &Listeners::keyboard, this);
    } else if (!has_keyboard && keyboard_ != nullptr) {
        stop_key_repeat();
        wl_keyboard_release(keyboard_);
        keyboard_ = nullptr;
        keyboard_window_ = nullptr;
    }
}

void App::handle_keymap(uint32_t format, int fd, uint32_t size) {
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }
    void* data = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (data == MAP_FAILED) {
        return;
    }
    // `size` includes the terminating NUL.
    xkb_keymap* keymap = xkb_keymap_new_from_buffer(xkb_context_, static_cast<const char*>(data),
                                                    size - 1, XKB_KEYMAP_FORMAT_TEXT_V1,
                                                    XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(data, size);
    if (keymap == nullptr) {
        return;
    }
    if (xkb_state_ != nullptr) {
        xkb_state_unref(xkb_state_);
    }
    if (xkb_keymap_ != nullptr) {
        xkb_keymap_unref(xkb_keymap_);
    }
    xkb_keymap_ = keymap;
    xkb_state_ = xkb_state_new(keymap);
}

void App::emit_key(uint32_t key, bool pressed) {
    if (xkb_state_ == nullptr || keyboard_window_ == nullptr) {
        return;
    }
    const xkb_keycode_t code = key + 8;
    if (pressed && keyboard_window_->on_key) {
        // A window that takes keys itself (the terminal) sees them first.
        const xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_state_, code);
        auto active = [&](const char* mod) {
            return xkb_state_mod_name_is_active(xkb_state_, mod, XKB_STATE_MODS_EFFECTIVE) > 0;
        };
        const KeyEvent event{.keysym = sym,
                             .codepoint = xkb_keysym_to_utf32(sym),
                             .ctrl = active(XKB_MOD_NAME_CTRL),
                             .shift = active(XKB_MOD_NAME_SHIFT),
                             .alt = active(XKB_MOD_NAME_ALT),
                             .super = active(XKB_MOD_NAME_LOGO)};
        if (keyboard_window_->on_key(event)) {
            return;
        }
    }
    // Identify keys by their unshifted symbol, so Ctrl+Shift+A is still "A".
    const xkb_keysym_t* syms = nullptr;
    const int count = xkb_keymap_key_get_syms_by_level(
        xkb_keymap_, code, xkb_state_key_get_layout(xkb_state_, code), 0, &syms);
    if (count > 0) {
        if (const int imgui_key = keysym_to_imgui_key(syms[0]); imgui_key != ImGuiKey_None) {
            keyboard_window_->handle_key(imgui_key, pressed);
        }
    }
    if (pressed) {
        std::array<char, 64> text{};
        const int len = xkb_state_key_get_utf8(xkb_state_, code, text.data(), text.size());
        const auto first = static_cast<unsigned char>(text[0]);
        if (len > 0 && first >= 0x20 && first != 0x7f) {
            keyboard_window_->handle_text(text.data());
        }
    }
}

void App::handle_key(uint32_t key, bool pressed) {
    emit_key(key, pressed);
    if (pressed && xkb_keymap_ != nullptr && xkb_keymap_key_repeats(xkb_keymap_, key + 8) != 0) {
        start_key_repeat(key);
    } else if (!pressed && key == repeat_key_) {
        stop_key_repeat();
    }
}

void App::start_key_repeat(uint32_t key) {
    stop_key_repeat();
    if (repeat_rate_ <= 0) {
        return;
    }
    repeat_key_ = key;
    repeat_timer_ = add_timer(std::chrono::milliseconds(repeat_delay_), [this] { repeat_tick(); });
}

void App::repeat_tick() {
    emit_key(repeat_key_, true);
    repeat_timer_ = add_timer(std::chrono::milliseconds(1000 / repeat_rate_), [this] { repeat_tick(); });
}

void App::stop_key_repeat() {
    if (repeat_timer_) {
        cancel_timer(*repeat_timer_);
        repeat_timer_.reset();
    }
    repeat_key_ = 0;
}

void App::handle_modifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
    if (xkb_state_ == nullptr) {
        return;
    }
    xkb_state_update_mask(xkb_state_, depressed, latched, locked, 0, 0, group);
    if (keyboard_window_ == nullptr) {
        return;
    }
    auto active = [&](const char* name) {
        return xkb_state_mod_name_is_active(xkb_state_, name, XKB_STATE_MODS_EFFECTIVE) > 0;
    };
    keyboard_window_->handle_modifiers(active(XKB_MOD_NAME_CTRL), active(XKB_MOD_NAME_SHIFT),
                                       active(XKB_MOD_NAME_ALT), active(XKB_MOD_NAME_LOGO));
}

void App::set_cursor(int imgui_cursor) {
    if (imgui_cursor == cursor_ || pointer_ == nullptr) {
        return;
    }
    cursor_ = imgui_cursor;
    if (imgui_cursor == ImGuiMouseCursor_None) {
        wl_pointer_set_cursor(pointer_, pointer_serial_, nullptr, 0, 0);
    } else if (cursor_shape_device_ != nullptr) {
        wp_cursor_shape_device_v1_set_shape(cursor_shape_device_, pointer_serial_,
                                            cursor_shape(imgui_cursor));
    }
}

// --- windows -------------------------------------------------------------------------

void App::register_window(Window* window) {
    windows_.push_back(window);
}

void App::unregister_window(Window* window) {
    std::erase(windows_, window);
    if (pointer_window_ == window) {
        pointer_window_ = nullptr;
    }
    if (keyboard_window_ == window) {
        keyboard_window_ = nullptr;
        stop_key_repeat();
    }
}

Window* App::window_for(wl_surface* surface) const {
    auto it = std::ranges::find_if(windows_, [&](const Window* w) { return w->surface() == surface; });
    return it != windows_.end() ? *it : nullptr;
}

void App::render_windows() {
    const std::vector<Window*> windows = windows_; // rendering may create/destroy windows
    for (Window* window : windows) {
        if (std::ranges::find(windows_, window) != windows_.end() && window->wants_render()) {
            window->render();
        }
    }
}

// --- timers, fds, loop -----------------------------------------------------------------

App::TimerId App::add_timer(std::chrono::milliseconds delay, std::function<void()> callback) {
    const TimerId id = next_timer_id_++;
    timers_.push_back({id, std::chrono::steady_clock::now() + delay, std::move(callback)});
    return id;
}

void App::cancel_timer(TimerId id) {
    std::erase_if(timers_, [&](const Timer& t) { return t.id == id; });
}

void App::watch_fd(int fd, std::function<void()> on_readable) {
    fds_.push_back({fd, std::move(on_readable)});
}

void App::unwatch_fd(int fd) {
    std::erase_if(fds_, [&](const WatchedFd& w) { return w.fd == fd; });
}

App::SourceId App::add_source(std::function<int(std::vector<pollfd>&)> prepare,
                              std::function<void()> dispatch) {
    const SourceId id = next_source_id_++;
    sources_.push_back({id, std::move(prepare), std::move(dispatch)});
    return id;
}

void App::remove_source(SourceId id) {
    std::erase_if(sources_, [&](const Source& s) { return s.id == id; });
}

int App::poll_timeout_ms() const {
    if (timers_.empty()) {
        return -1;
    }
    const auto next = std::ranges::min_element(timers_, {}, &Timer::due)->due;
    const auto wait = std::chrono::ceil<std::chrono::milliseconds>(next - std::chrono::steady_clock::now());
    return static_cast<int>(std::max<int64_t>(0, wait.count()));
}

void App::run_due_timers() {
    const auto now = std::chrono::steady_clock::now();
    std::vector<Timer> due;
    std::erase_if(timers_, [&](Timer& t) {
        if (t.due <= now) {
            due.push_back(std::move(t));
            return true;
        }
        return false;
    });
    for (auto& timer : due) {
        timer.callback();
    }
}

void App::quit(int code) {
    exit_code_ = code;
    running_ = false;
}

int App::run() {
    started_ = true;
    for (const auto& output : outputs_) {
        if (output->info.done && on_output_added) {
            on_output_added(output->info);
        }
    }

    const int display_fd = wl_display_get_fd(display_);
    std::vector<pollfd> pollfds;
    while (running_) {
        render_windows();

        while (wl_display_prepare_read(display_) != 0) {
            if (wl_display_dispatch_pending(display_) < 0) {
                return 1;
            }
        }
        wl_display_flush(display_);

        pollfds.assign(1, pollfd{.fd = display_fd, .events = POLLIN, .revents = 0});
        for (const auto& w : fds_) {
            pollfds.push_back({.fd = w.fd, .events = POLLIN, .revents = 0});
        }
        const std::size_t watched_end = pollfds.size();
        int timeout = poll_timeout_ms();
        for (const auto& source : sources_) {
            const int source_timeout = source.prepare(pollfds);
            if (source_timeout >= 0 && (timeout < 0 || source_timeout < timeout)) {
                timeout = source_timeout;
            }
        }
        const int ready = poll(pollfds.data(), pollfds.size(), timeout);
        if (ready > 0 && (pollfds[0].revents & POLLIN) != 0) {
            wl_display_read_events(display_);
        } else {
            wl_display_cancel_read(display_);
        }
        if ((pollfds[0].revents & (POLLERR | POLLHUP)) != 0) {
            std::println(stderr, "{}: lost connection to the compositor", app_id_);
            return 1;
        }
        if (wl_display_dispatch_pending(display_) < 0) {
            std::println(stderr, "{}: Wayland protocol error", app_id_);
            return 1;
        }
        for (std::size_t i = 1; i < watched_end; ++i) {
            if ((pollfds[i].revents & POLLIN) != 0) {
                const int fd = pollfds[i].fd;
                auto it = std::ranges::find_if(fds_, [&](const WatchedFd& w) { return w.fd == fd; });
                if (it != fds_.end()) {
                    auto callback = it->callback; // may unwatch itself
                    callback();
                }
            }
        }
        // Sources dispatch unconditionally: cheap, and they know best.
        const auto sources = sources_; // dispatch may add or remove sources
        for (const auto& source : sources) {
            source.dispatch();
        }
        run_due_timers();
    }
    return exit_code_;
}

} // namespace tetra::ui
