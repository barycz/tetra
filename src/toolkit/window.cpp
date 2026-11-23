#include "window.hpp"

#include "app.hpp"
#include "protocols.hpp"
#include "style.hpp"

#include <GLES3/gl3.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <linux/input-event-codes.h>
#include <wayland-egl.h>

#include <algorithm>
#include <cfloat>

namespace tetra::ui {

namespace {

const wl_surface_listener kSurfaceListener = {
    .enter = [](void* /*data*/, wl_surface* /*surface*/, wl_output* /*output*/) {},
    .leave = [](void* /*data*/, wl_surface* /*surface*/, wl_output* /*output*/) {},
    .preferred_buffer_scale = [](void* data, wl_surface* /*surface*/,
                                 int32_t factor) { static_cast<Window*>(data)->handle_scale(factor); },
    .preferred_buffer_transform = [](void* /*data*/, wl_surface* /*surface*/,
                                     uint32_t /*transform*/) {},
};

const wl_callback_listener kFrameListener = {
    .done =
        [](void* data, wl_callback* /*callback*/, uint32_t /*time*/) {
            static_cast<Window*>(data)->handle_frame_done();
        },
};

// Makes `context` current for the lifetime of the guard.
class ContextGuard {
public:
    explicit ContextGuard(ImGuiContext* context) : previous_(ImGui::GetCurrentContext()) {
        ImGui::SetCurrentContext(context);
    }
    ~ContextGuard() { ImGui::SetCurrentContext(previous_); }
    ContextGuard(const ContextGuard&) = delete;
    ContextGuard& operator=(const ContextGuard&) = delete;
    ContextGuard(ContextGuard&&) = delete;
    ContextGuard& operator=(ContextGuard&&) = delete;

private:
    ImGuiContext* previous_;
};

// Restores the EGL surfaces/context current at construction. Windows can be
// created or destroyed while another window is drawing (e.g. the bar opening
// the launcher from a click); that must not redirect the drawing window's
// frame to another surface.
class EglGuard {
public:
    EglGuard()
        : display_(eglGetCurrentDisplay()), draw_(eglGetCurrentSurface(EGL_DRAW)),
          read_(eglGetCurrentSurface(EGL_READ)), context_(eglGetCurrentContext()) {}
    ~EglGuard() {
        if (display_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(display_, draw_, read_, context_);
        }
    }
    // `surface` is about to be destroyed: do not restore it.
    void forget(EGLSurface surface) {
        if (draw_ == surface || read_ == surface) {
            draw_ = EGL_NO_SURFACE;
            read_ = EGL_NO_SURFACE;
        }
    }
    EglGuard(const EglGuard&) = delete;
    EglGuard& operator=(const EglGuard&) = delete;
    EglGuard(EglGuard&&) = delete;
    EglGuard& operator=(EglGuard&&) = delete;

private:
    EGLDisplay display_;
    EGLSurface draw_;
    EGLSurface read_;
    EGLContext context_;
};

} // namespace

// --- Window --------------------------------------------------------------------------

Window::Window(App& app) : app_(app), last_frame_(std::chrono::steady_clock::now()) {
    const EglGuard egl;
    surface_ = wl_compositor_create_surface(app_.compositor());
    wl_surface_add_listener(surface_, &kSurfaceListener, this);

    imgui_ = ImGui::CreateContext();
    const ContextGuard guard(imgui_);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.BackendPlatformName = "tetra-wayland";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    make_current();
    ImGui_ImplOpenGL3_Init("#version 300 es");
    apply_theme();
    app_.register_window(this);
}

Window::~Window() {
    EglGuard egl;
    egl.forget(egl_surface_);
    app_.unregister_window(this);
    {
        const ContextGuard guard(imgui_);
        make_current();
        ImGui_ImplOpenGL3_Shutdown();
    }
    ImGui::DestroyContext(imgui_);
    if (frame_callback_ != nullptr) {
        wl_callback_destroy(frame_callback_);
    }
    if (egl_surface_ != EGL_NO_SURFACE) {
        eglMakeCurrent(app_.egl_display(), EGL_NO_SURFACE, EGL_NO_SURFACE, app_.egl_context());
        eglDestroySurface(app_.egl_display(), egl_surface_);
    }
    if (egl_window_ != nullptr) {
        wl_egl_window_destroy(egl_window_);
    }
    wl_surface_destroy(surface_);
}

void Window::make_current() {
    eglMakeCurrent(app_.egl_display(), egl_surface_, egl_surface_, app_.egl_context());
}

void Window::apply_theme() {
    const ContextGuard guard(imgui_);
    const auto& theme = app_.config().theme;
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = static_cast<float>(theme.font_size);
    apply_style(style, app_.palette());

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFont* font = nullptr;
    if (!app_.font_path().empty()) {
        font = io.Fonts->AddFontFromFileTTF(app_.font_path().c_str(), style.FontSizeBase);
    }
    if (font == nullptr) {
        io.Fonts->AddFontDefault();
    }
    // Rebuilding the atlas resyncs FontSizeBase from ImGui's internal state;
    // this is how ImGui itself applies a size change on the next frame.
    style._NextFrameFontSizeBase = static_cast<float>(theme.font_size);
    request_redraw();
}

void Window::request_redraw(int frames) {
    frames_needed_ = std::max(frames_needed_, frames);
}

bool Window::wants_render() const noexcept {
    return configured_ && frames_needed_ > 0 && frame_callback_ == nullptr &&
           egl_surface_ != EGL_NO_SURFACE;
}

void Window::resize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    width_ = width;
    height_ = height;
    configured_ = true;
    ensure_egl_surface();
    request_redraw();
}

void Window::ensure_egl_surface() {
    const int pw = width_ * scale_;
    const int ph = height_ * scale_;
    if (egl_window_ == nullptr) {
        const EglGuard egl;
        egl_window_ = wl_egl_window_create(surface_, pw, ph);
        egl_surface_ = eglCreatePlatformWindowSurface(app_.egl_display(), app_.egl_config(),
                                                      egl_window_, nullptr);
        make_current();
        // We pace with frame callbacks ourselves; never block in SwapBuffers.
        eglSwapInterval(app_.egl_display(), 0);
    } else {
        wl_egl_window_resize(egl_window_, pw, ph, 0, 0);
    }
}

void Window::handle_scale(int scale) {
    if (scale == scale_ || scale < 1) {
        return;
    }
    scale_ = scale;
    wl_surface_set_buffer_scale(surface_, scale_);
    if (configured_) {
        ensure_egl_surface();
    }
    request_redraw();
}

void Window::render() {
    make_current();
    const ContextGuard guard(imgui_);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = {static_cast<float>(width_), static_cast<float>(height_)};
    io.DisplayFramebufferScale = {static_cast<float>(scale_), static_cast<float>(scale_)};
    const auto now = std::chrono::steady_clock::now();
    io.DeltaTime = std::clamp(std::chrono::duration<float>(now - last_frame_).count(), 0.001F, 0.25F);
    last_frame_ = now;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    if (on_draw) {
        on_draw();
    }
    ImGui::Render();

    // on_draw may have created or destroyed windows; draw into ours.
    make_current();
    glViewport(0, 0, width_ * scale_, height_ * scale_);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Ask for a frame callback before the commit in SwapBuffers; the next
    // frame (if any) waits for it, so hidden windows stop rendering.
    frame_callback_ = wl_surface_frame(surface_);
    wl_callback_add_listener(frame_callback_, &kFrameListener, this);
    if (eglSwapBuffers(app_.egl_display(), egl_surface_) == EGL_FALSE) {
        // Nothing was committed, so the callback would never fire and the
        // window would never render again.
        wl_callback_destroy(frame_callback_);
        frame_callback_ = nullptr;
    }
    --frames_needed_;

    // ImGui trickles input: a move, a press and a release queued together
    // take a frame each. Keep rendering until the queue is drained, or a
    // quick click is only seen at the next mouse move.
    if (!GImGui->InputEventsQueue.empty()) {
        request_redraw(1);
    }

    if (pointer_inside_) {
        app_.set_cursor(ImGui::GetMouseCursor());
    }
}

void Window::handle_frame_done() {
    if (frame_callback_ != nullptr) {
        wl_callback_destroy(frame_callback_);
        frame_callback_ = nullptr;
    }
}

void Window::handle_pointer_enter(double x, double y) {
    pointer_inside_ = true;
    handle_pointer_motion(x, y);
}

void Window::handle_pointer_leave() {
    pointer_inside_ = false;
    const ContextGuard guard(imgui_);
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    request_redraw();
}

void Window::handle_pointer_motion(double x, double y) {
    const ContextGuard guard(imgui_);
    ImGui::GetIO().AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
    request_redraw();
}

void Window::handle_pointer_button(uint32_t button, bool pressed) {
    int index = -1;
    switch (button) {
    case BTN_LEFT:
        index = ImGuiMouseButton_Left;
        break;
    case BTN_RIGHT:
        index = ImGuiMouseButton_Right;
        break;
    case BTN_MIDDLE:
        index = ImGuiMouseButton_Middle;
        break;
    case BTN_SIDE:
        index = 3;
        break;
    case BTN_EXTRA:
        index = 4;
        break;
    default:
        return;
    }
    const ContextGuard guard(imgui_);
    ImGui::GetIO().AddMouseButtonEvent(index, pressed);
    request_redraw();
}

void Window::handle_pointer_axis(double dx, double dy) {
    const ContextGuard guard(imgui_);
    ImGui::GetIO().AddMouseWheelEvent(static_cast<float>(dx), static_cast<float>(dy));
    request_redraw();
}

void Window::handle_keyboard_focus(bool focused) {
    const ContextGuard guard(imgui_);
    ImGui::GetIO().AddFocusEvent(focused);
    request_redraw();
}

void Window::handle_key(int imgui_key, bool pressed) {
    const ContextGuard guard(imgui_);
    ImGui::GetIO().AddKeyEvent(static_cast<ImGuiKey>(imgui_key), pressed);
    request_redraw();
}

void Window::handle_modifiers(bool ctrl, bool shift, bool alt, bool super) {
    const ContextGuard guard(imgui_);
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
    io.AddKeyEvent(ImGuiMod_Shift, shift);
    io.AddKeyEvent(ImGuiMod_Alt, alt);
    io.AddKeyEvent(ImGuiMod_Super, super);
    request_redraw();
}

void Window::handle_text(const char* utf8) {
    const ContextGuard guard(imgui_);
    ImGui::GetIO().AddInputCharactersUTF8(utf8);
    request_redraw();
}

// --- LayerWindow ---------------------------------------------------------------------

struct LayerListeners {
    static const zwlr_layer_surface_v1_listener listener;
};

const zwlr_layer_surface_v1_listener LayerListeners::listener = {
    .configure =
        [](void* data, zwlr_layer_surface_v1* surface, uint32_t serial, uint32_t width,
           uint32_t height) {
            auto* self = static_cast<LayerWindow*>(data);
            zwlr_layer_surface_v1_ack_configure(surface, serial);
            self->resize(width != 0 ? static_cast<int>(width) : self->options_.width,
                         height != 0 ? static_cast<int>(height) : self->options_.height);
        },
    .closed =
        [](void* data, zwlr_layer_surface_v1* /*surface*/) {
            auto* self = static_cast<LayerWindow*>(data);
            if (self->on_closed) {
                self->on_closed();
            }
        },
};

LayerWindow::LayerWindow(App& app, const Options& options) : Window(app), options_(options) {
    layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(
        app_.layer_shell(), surface_, options_.output, static_cast<uint32_t>(options_.layer),
        options_.name_space.c_str());
    zwlr_layer_surface_v1_set_size(layer_surface_, static_cast<uint32_t>(options_.width),
                                   static_cast<uint32_t>(options_.height));
    zwlr_layer_surface_v1_set_anchor(layer_surface_, options_.anchor);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, options_.exclusive_zone);
    zwlr_layer_surface_v1_set_margin(layer_surface_, options_.margin.top, options_.margin.right,
                                     options_.margin.bottom, options_.margin.left);
    zwlr_layer_surface_v1_set_keyboard_interactivity(layer_surface_,
                                                     static_cast<uint32_t>(options_.keyboard));
    zwlr_layer_surface_v1_add_listener(layer_surface_, &LayerListeners::listener, this);
    wl_surface_commit(surface_);
}

LayerWindow::~LayerWindow() {
    zwlr_layer_surface_v1_destroy(layer_surface_);
}

void LayerWindow::set_exclusive_zone(int zone) {
    options_.exclusive_zone = zone;
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, zone);
    request_redraw();
}

void LayerWindow::set_size(int width, int height) {
    if (width == options_.width && height == options_.height) {
        return;
    }
    options_.width = width;
    options_.height = height;
    zwlr_layer_surface_v1_set_size(layer_surface_, static_cast<uint32_t>(width),
                                   static_cast<uint32_t>(height));
    // State requests take effect with a commit; the compositor answers with
    // a configure, which resizes the EGL surface and redraws.
    wl_surface_commit(surface_);
}

// --- ToplevelWindow ------------------------------------------------------------------

struct ToplevelListeners {
    static const xdg_surface_listener surface;
    static const xdg_toplevel_listener toplevel;
    static const zxdg_toplevel_decoration_v1_listener decoration;
};

const xdg_surface_listener ToplevelListeners::surface = {
    .configure =
        [](void* data, xdg_surface* surface, uint32_t serial) {
            auto* self = static_cast<ToplevelWindow*>(data);
            xdg_surface_ack_configure(surface, serial);
            self->resize(self->pending_width_ > 0 ? self->pending_width_ : self->options_.width,
                         self->pending_height_ > 0 ? self->pending_height_ : self->options_.height);
        },
};

const xdg_toplevel_listener ToplevelListeners::toplevel = {
    .configure =
        [](void* data, xdg_toplevel* /*toplevel*/, int32_t width, int32_t height,
           wl_array* /*states*/) {
            auto* self = static_cast<ToplevelWindow*>(data);
            self->pending_width_ = width;
            self->pending_height_ = height;
        },
    .close =
        [](void* data, xdg_toplevel* /*toplevel*/) {
            auto* self = static_cast<ToplevelWindow*>(data);
            if (self->on_close) {
                self->on_close();
            } else {
                self->app_.quit();
            }
        },
    .configure_bounds = [](void* /*data*/, xdg_toplevel* /*toplevel*/, int32_t /*width*/,
                           int32_t /*height*/) {},
    .wm_capabilities = [](void* /*data*/, xdg_toplevel* /*toplevel*/, wl_array* /*caps*/) {},
};

const zxdg_toplevel_decoration_v1_listener ToplevelListeners::decoration = {
    .configure = [](void* /*data*/, zxdg_toplevel_decoration_v1* /*decoration*/,
                    uint32_t /*mode*/) {},
};

ToplevelWindow::ToplevelWindow(App& app, const Options& options) : Window(app), options_(options) {
    xdg_surface_ = xdg_wm_base_get_xdg_surface(app_.wm_base(), surface_);
    xdg_surface_add_listener(xdg_surface_, &ToplevelListeners::surface, this);
    xdg_toplevel_ = xdg_surface_get_toplevel(xdg_surface_);
    xdg_toplevel_add_listener(xdg_toplevel_, &ToplevelListeners::toplevel, this);
    xdg_toplevel_set_title(xdg_toplevel_, options_.title.c_str());
    xdg_toplevel_set_app_id(xdg_toplevel_, app_.app_id().c_str());
    if (app_.decoration_manager() != nullptr) {
        decoration_ = zxdg_decoration_manager_v1_get_toplevel_decoration(app_.decoration_manager(),
                                                                         xdg_toplevel_);
        zxdg_toplevel_decoration_v1_add_listener(decoration_, &ToplevelListeners::decoration, this);
        zxdg_toplevel_decoration_v1_set_mode(decoration_,
                                             ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }
    wl_surface_commit(surface_);
}

ToplevelWindow::~ToplevelWindow() {
    if (decoration_ != nullptr) {
        zxdg_toplevel_decoration_v1_destroy(decoration_);
    }
    xdg_toplevel_destroy(xdg_toplevel_);
    xdg_surface_destroy(xdg_surface_);
}

void ToplevelWindow::set_title(const std::string& title) {
    options_.title = title;
    xdg_toplevel_set_title(xdg_toplevel_, title.c_str());
}

} // namespace tetra::ui
