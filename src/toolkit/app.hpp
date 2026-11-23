#pragma once

#include "config.hpp"
#include "file_watcher.hpp"
#include "theme.hpp"

#include <EGL/egl.h>
#include <poll.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct wp_cursor_shape_device_v1;
struct wp_cursor_shape_manager_v1;
struct xdg_wm_base;
struct zwlr_layer_shell_v1;
struct zxdg_decoration_manager_v1;

namespace tetra::ui {

class Window;

struct OutputInfo {
    uint32_t global = 0;
    wl_output* output = nullptr;
    std::string name;
    std::string description;
    int width = 0;
    int height = 0;
    int scale = 1;
    bool done = false;
};

// Connection to the compositor and the event loop shared by every tetra UI
// process. Owns Wayland globals, input devices, the EGL context, the
// configuration (with live reload) and timers. Windows render on demand only.
class App {
public:
    static std::expected<std::unique_ptr<App>, std::string> create(std::string app_id);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    // Runs until quit(); returns the exit code.
    int run();
    void quit(int code = 0);

    // --- timers and file descriptors -------------------------------------------
    using TimerId = uint64_t;
    TimerId add_timer(std::chrono::milliseconds delay, std::function<void()> callback);
    void cancel_timer(TimerId id);
    void watch_fd(int fd, std::function<void()> on_readable);
    void unwatch_fd(int fd);

    // A source that recomputes what to poll every loop iteration (e.g. a
    // D-Bus connection): `prepare` appends its pollfds and returns a timeout
    // in ms (-1: none); `dispatch` runs after every poll.
    using SourceId = uint64_t;
    SourceId add_source(std::function<int(std::vector<pollfd>&)> prepare,
                        std::function<void()> dispatch);
    void remove_source(SourceId id);

    // --- outputs ----------------------------------------------------------------
    // Called for each output once its properties are known (also for outputs
    // that existed before run()), and when it goes away.
    std::function<void(const OutputInfo&)> on_output_added;
    std::function<void(const OutputInfo&)> on_output_removed;

    // --- configuration ------------------------------------------------------------
    [[nodiscard]] const config::Config& config() const noexcept { return config_; }
    [[nodiscard]] const theme::Palette& palette() const noexcept { return palette_; }
    [[nodiscard]] const std::string& font_path() const noexcept { return font_path_; }
    // After windows have re-applied the theme.
    std::function<void()> on_config_changed;

    // --- for Window -------------------------------------------------------------------
    [[nodiscard]] wl_display* display() const noexcept { return display_; }
    [[nodiscard]] wl_compositor* compositor() const noexcept { return compositor_; }
    [[nodiscard]] xdg_wm_base* wm_base() const noexcept { return wm_base_; }
    [[nodiscard]] zwlr_layer_shell_v1* layer_shell() const noexcept { return layer_shell_; }
    [[nodiscard]] zxdg_decoration_manager_v1* decoration_manager() const noexcept {
        return decoration_manager_;
    }
    [[nodiscard]] EGLDisplay egl_display() const noexcept { return egl_display_; }
    [[nodiscard]] EGLConfig egl_config() const noexcept { return egl_config_; }
    [[nodiscard]] EGLContext egl_context() const noexcept { return egl_context_; }
    [[nodiscard]] const std::string& app_id() const noexcept { return app_id_; }
    void register_window(Window* window);
    void unregister_window(Window* window);
    void set_cursor(int imgui_cursor);
    // Makes the shared GL context current (for creating/deleting textures).
    void make_gl_current() const;

    // Binds a global not handled by App (e.g. a private protocol); null if
    // the compositor does not offer it to us.
    void* bind_global(const wl_interface* interface, uint32_t max_version);

private:
    explicit App(std::string app_id);
    std::expected<void, std::string> init();
    std::expected<void, std::string> init_egl();
    void load_config(bool initial);
    void render_windows();
    void run_due_timers();
    [[nodiscard]] int poll_timeout_ms() const;

    // Wayland event handlers (app.cpp / input.cpp).
    friend struct Listeners;
    void handle_global(wl_registry* registry, uint32_t name, const char* interface,
                       uint32_t version);
    void handle_global_remove(uint32_t name);
    void handle_seat_capabilities(uint32_t caps);
    void handle_keymap(uint32_t format, int fd, uint32_t size);
    void handle_key(uint32_t key, bool pressed);
    void emit_key(uint32_t key, bool pressed);
    void handle_modifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
    void start_key_repeat(uint32_t key);
    void repeat_tick();
    void stop_key_repeat();
    [[nodiscard]] Window* window_for(wl_surface* surface) const;

    std::string app_id_;
    bool running_ = true;
    int exit_code_ = 0;
    bool started_ = false;

    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_compositor* compositor_ = nullptr;
    xdg_wm_base* wm_base_ = nullptr;
    zwlr_layer_shell_v1* layer_shell_ = nullptr;
    zxdg_decoration_manager_v1* decoration_manager_ = nullptr;
    wp_cursor_shape_manager_v1* cursor_shape_manager_ = nullptr;
    wl_seat* seat_ = nullptr;
    wl_pointer* pointer_ = nullptr;
    wl_keyboard* keyboard_ = nullptr;
    wp_cursor_shape_device_v1* cursor_shape_device_ = nullptr;
    struct Output {
        App* app;
        OutputInfo info;
    };
    std::vector<std::unique_ptr<Output>> outputs_;
    struct Global {
        uint32_t name;
        std::string interface;
        uint32_t version;
    };
    std::vector<Global> globals_;

    EGLDisplay egl_display_ = EGL_NO_DISPLAY;
    EGLConfig egl_config_ = nullptr;
    EGLContext egl_context_ = EGL_NO_CONTEXT;

    std::vector<Window*> windows_;

    // pointer
    Window* pointer_window_ = nullptr;
    uint32_t pointer_serial_ = 0;
    int cursor_ = -2; // ImGuiMouseCursor; -2 = not set yet

    // keyboard
    Window* keyboard_window_ = nullptr;
    xkb_context* xkb_context_ = nullptr;
    xkb_keymap* xkb_keymap_ = nullptr;
    xkb_state* xkb_state_ = nullptr;
    int repeat_rate_ = 25;
    int repeat_delay_ = 600;
    std::optional<TimerId> repeat_timer_;
    uint32_t repeat_key_ = 0;

    // timers & fds
    struct Timer {
        TimerId id;
        std::chrono::steady_clock::time_point due;
        std::function<void()> callback;
    };
    std::vector<Timer> timers_;
    TimerId next_timer_id_ = 1;
    struct WatchedFd {
        int fd;
        std::function<void()> callback;
    };
    std::vector<WatchedFd> fds_;
    struct Source {
        SourceId id;
        std::function<int(std::vector<pollfd>&)> prepare;
        std::function<void()> dispatch;
    };
    std::vector<Source> sources_;
    SourceId next_source_id_ = 1;

    // configuration
    config::Config config_;
    theme::Palette palette_;
    std::string font_path_;
    std::optional<FileWatcher> config_watcher_;
};

} // namespace tetra::ui
