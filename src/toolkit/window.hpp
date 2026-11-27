#pragma once

#include <EGL/egl.h>
#include <wayland-client.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

struct ImGuiContext;
struct ext_session_lock_surface_v1;
struct ext_session_lock_v1;
struct wl_egl_window;
struct xdg_surface;
struct xdg_toplevel;
struct zwlr_layer_surface_v1;
struct zxdg_toplevel_decoration_v1;
struct zxdg_imported_v2;
struct zxdg_importer_v2;

namespace tetra::ui {

class App;
struct KeyEvent;

// A Wayland surface with its own EGL surface and ImGui context. Renders only
// when something changed: input, a resize, or request_redraw().
class Window {
public:
    virtual ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    // Called inside an ImGui frame; draw the whole window content.
    std::function<void()> on_draw;
    // Key presses before ImGui sees them; return true to keep a key from
    // ImGui. For windows that interpret keys themselves (the terminal).
    std::function<bool(const KeyEvent&)> on_key;
    // Called (with this window's ImGui context current) whenever the font
    // atlas is rebuilt, after the UI font: add more fonts here.
    std::function<void()> on_load_fonts;

    // Render at least `frames` more frames (ImGui needs a couple to settle).
    void request_redraw(int frames = 2);
    // For this frame (call from on_draw): draw everything with `alpha` and
    // moved by (dx, dy) pixels. Panels fading in (motion.hpp).
    void set_fade(float alpha, float dx = 0, float dy = 0) { fade_ = {alpha, dx, dy}; }
    void apply_theme();

    [[nodiscard]] wl_surface* surface() const noexcept { return surface_; }
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] ImGuiContext* imgui() const noexcept { return imgui_; }

    // --- used by App -----------------------------------------------------------------
    [[nodiscard]] bool wants_render() const noexcept;
    void render();
    void handle_pointer_enter(double x, double y);
    void handle_pointer_leave();
    void handle_pointer_motion(double x, double y);
    void handle_pointer_button(uint32_t button, bool pressed);
    void handle_pointer_axis(double dx, double dy);
    void handle_keyboard_focus(bool focused);
    void handle_key(int imgui_key, bool pressed);
    void handle_modifiers(bool ctrl, bool shift, bool alt, bool super);
    void handle_text(const char* utf8);
    void handle_scale(int scale);
    void handle_frame_done();

protected:
    explicit Window(App& app);
    // Logical size from the compositor's configure.
    void resize(int width, int height);
    void make_current();

    App& app_;
    wl_surface* surface_ = nullptr;

private:
    void ensure_egl_surface();

    ImGuiContext* imgui_ = nullptr;
    wl_egl_window* egl_window_ = nullptr;
    EGLSurface egl_surface_ = EGL_NO_SURFACE;
    wl_callback* frame_callback_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int scale_ = 1;
    int frames_needed_ = 0;
    int focus_frames_ = 0; // frames rendered for a pending focus request
    struct {
        float alpha = 1;
        float dx = 0;
        float dy = 0;
    } fade_;
    bool configured_ = false;
    bool pointer_inside_ = false;
    std::chrono::steady_clock::time_point last_frame_;
};

// A wlr-layer-shell surface: bars, panels, overlays.
class LayerWindow : public Window {
public:
    enum class Layer { Background = 0, Bottom = 1, Top = 2, Overlay = 3 };
    enum class Keyboard { None = 0, Exclusive = 1, OnDemand = 2 };
    // Anchor bits, as in zwlr_layer_surface_v1.anchor.
    static constexpr uint32_t kTop = 1;
    static constexpr uint32_t kBottom = 2;
    static constexpr uint32_t kLeft = 4;
    static constexpr uint32_t kRight = 8;

    struct Options {
        Layer layer = Layer::Top;
        uint32_t anchor = 0;
        int width = 0;  // 0: stretch between left/right anchors
        int height = 0; // 0: stretch between top/bottom anchors
        int exclusive_zone = 0;
        Keyboard keyboard = Keyboard::None;
        std::string name_space = "tetra";
        wl_output* output = nullptr; // null: compositor's choice
        struct {
            int top = 0;
            int right = 0;
            int bottom = 0;
            int left = 0;
        } margin{};
    };

    LayerWindow(App& app, const Options& options);
    ~LayerWindow() override;

    LayerWindow(const LayerWindow&) = delete;
    LayerWindow& operator=(const LayerWindow&) = delete;
    LayerWindow(LayerWindow&&) = delete;
    LayerWindow& operator=(LayerWindow&&) = delete;

    void set_exclusive_zone(int zone);
    // Asks the compositor for a new size (applied with its next configure).
    void set_size(int width, int height);
    [[nodiscard]] int requested_height() const noexcept { return options_.height; }

    // The compositor closed the surface (e.g. its output went away).
    std::function<void()> on_closed;

private:
    friend struct LayerListeners;
    Options options_;
    zwlr_layer_surface_v1* layer_surface_ = nullptr;
};

// An ext-session-lock surface: covers one output while the session is
// locked. The compositor decides its size.
class LockWindow : public Window {
public:
    LockWindow(App& app, ext_session_lock_v1* lock, wl_output* output);
    ~LockWindow() override;

    LockWindow(const LockWindow&) = delete;
    LockWindow& operator=(const LockWindow&) = delete;
    LockWindow(LockWindow&&) = delete;
    LockWindow& operator=(LockWindow&&) = delete;

private:
    friend struct LockListeners;
    ext_session_lock_surface_v1* lock_surface_ = nullptr;
};

// A regular application window with server-side decorations.
class ToplevelWindow : public Window {
public:
    struct Options {
        std::string title;
        int width = 800;
        int height = 600;
        // A window of another client exported with xdg-foreign v2 (a
        // portal's parent_window "wayland:HANDLE"): this one becomes its
        // child (a dialog placed over it and kept above it).
        std::string parent;
    };

    ToplevelWindow(App& app, const Options& options);
    ~ToplevelWindow() override;

    ToplevelWindow(const ToplevelWindow&) = delete;
    ToplevelWindow& operator=(const ToplevelWindow&) = delete;
    ToplevelWindow(ToplevelWindow&&) = delete;
    ToplevelWindow& operator=(ToplevelWindow&&) = delete;

    void set_title(const std::string& title);

    // The user asked to close the window; defaults to quitting the app.
    std::function<void()> on_close;

private:
    friend struct ToplevelListeners;
    Options options_;
    xdg_surface* xdg_surface_ = nullptr;
    xdg_toplevel* xdg_toplevel_ = nullptr;
    zxdg_toplevel_decoration_v1* decoration_ = nullptr;
    zxdg_importer_v2* importer_ = nullptr;
    zxdg_imported_v2* imported_ = nullptr;
    int pending_width_ = 0;
    int pending_height_ = 0;
};

} // namespace tetra::ui
