#pragma once

#include "config.hpp"
#include "decoration.hpp"
#include "desktop_entry.hpp"
#include "file_watcher.hpp"
#include "listener.hpp"
#include "taskbar.hpp"
#include "theme.hpp"
#include "window_memory.hpp"
#include "wlr.hpp"
#include "zone_overlay.hpp"
#include "zones.hpp"

#include <chrono>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tetra {

class Keyboard;
class Pointer;
class LayerSurface;
class Output;
class SessionLock;
class ShellProtocol;
class View;

struct Options {
    std::optional<std::string> startup_command;
    std::optional<std::string> config_path; // overrides ~/.config/tetra/config.json
};

// Owns the Wayland display, the wlroots objects and every compositor-side
// object. One instance per process. Implementation is split across
// server.cpp (setup, config, processes), wm.cpp (windows, focus, layers) and
// input.cpp (pointer, keyboard bindings, interactive move/resize).
class Server {
public:
    static std::expected<std::unique_ptr<Server>, std::string> create(Options options);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;

    // Opens the socket, starts the backend, the shell and autostart entries,
    // then runs the event loop until terminate().
    std::expected<void, std::string> run();
    void terminate();
    pid_t spawn(const std::string& command);

    [[nodiscard]] const config::Config& config() const noexcept { return config_; }
    [[nodiscard]] const theme::Palette& palette() const noexcept { return palette_; }
    [[nodiscard]] const DesktopDatabase& desktop_entries() const noexcept { return desktop_entries_; }
    [[nodiscard]] Taskbar& taskbar() noexcept { return taskbar_; }
    [[nodiscard]] View* focused_view() const noexcept { return focused_view_; }
    // Super tap: ask the shell to open its launcher.
    void show_launcher();
    // Focus a window of one of tetra's apps, or start it (Super+I:
    // "tetra-settings" from "apps/settings").
    void open_app(std::string_view app_id, const std::string& build_dir);

    // --- session lock and idle (server.cpp) -------------------------------------
    // Asks the shell to lock the session (Super+L, or a lock client died).
    void request_lock();
    [[nodiscard]] bool locked() const noexcept;
    [[nodiscard]] bool lock_abandoned() const noexcept;
    // Keyboard and pointer focus follow the lock state.
    void handle_lock_changed();
    void handle_output_frame(wlr_output* output);
    // User input: resets idle timers (ext-idle-notify).
    void notify_activity();

    // Print key variants: ask the shell to capture (input.cpp).
    bool take_screenshot(uint32_t mode);

    // --- outputs & layers (wm.cpp) -------------------------------------------
    [[nodiscard]] Output* find_output(wlr_output* wlr_output) const;
    [[nodiscard]] wlr_box usable_area_at_cursor() const;
    [[nodiscard]] wlr_scene_tree* layer_tree(zwlr_layer_shell_v1_layer layer) const;
    void handle_usable_area_changed(Output& output);
    void handle_layer_mapped(LayerSurface& surface);
    void handle_layer_unmapped(LayerSurface& surface);
    void handle_layer_changed(LayerSurface& surface);
    void handle_layer_destroyed(LayerSurface& surface);
    void destroy_output(Output& output);
    // Publishes the outputs' state (wlr-output-management) once idle.
    void schedule_output_manager_update();

    // --- windows & focus (wm.cpp) --------------------------------------------
    void focus_view(View* view);
    // Raises the windows whose parent is `parent` (and theirs) above it.
    void raise_children(View* parent);
    [[nodiscard]] const std::vector<std::unique_ptr<View>>& views() const noexcept { return views_; }
    void focus_next_view();
    void set_keyboard_focus(wlr_surface* surface);
    void restore_keyboard_focus();
    void handle_view_mapped(View& view);
    void handle_view_unmapped(View& view);
    void handle_view_minimized(View& view);
    void handle_view_title_changed(View& view);
    void destroy_view(View& view);

    // --- window memory (placement.cpp) ---------------------------------------
    [[nodiscard]] windows::Memory& window_memory() noexcept { return window_memory_; }
    // Saves the memory a moment later.
    void window_memory_changed();
    // Displays in the layout, by display_key().
    [[nodiscard]] std::vector<std::string> display_keys() const;
    [[nodiscard]] wlr_output* output_for_key(std::string_view key) const;
    [[nodiscard]] wlr_output* output_at_cursor() const;
    // True while `view` is being moved or resized with the pointer.
    [[nodiscard]] bool grabbing(const View& view) const noexcept {
        return (grab_.mode == GrabMode::Move || grab_.mode == GrabMode::Resize) && grab_.view == &view;
    }

    // --- input (input.cpp) -----------------------------------------------------
    void begin_interactive_move(View& view);
    void begin_interactive_resize(View& view, uint32_t edges);
    bool handle_keybinding(uint32_t modifiers, xkb_keysym_t sym);
    void destroy_keyboard(Keyboard& keyboard);
    void destroy_pointer(Pointer& pointer);
    // Modifier keys changed (Shift shows snap zones while dragging).
    void handle_modifiers();

    // --- snap zones (input.cpp) ----------------------------------------------
    // The zones of a display, in layout coordinates (none: no layout).
    [[nodiscard]] std::vector<zones::Box> zones_for(wlr_output* output) const;

    // Scene layers, bottom to top.
    struct Layers {
        wlr_scene_tree* background = nullptr;
        wlr_scene_tree* bottom = nullptr;
        wlr_scene_tree* windows = nullptr;
        wlr_scene_tree* zones = nullptr; // snap zones while dragging
        wlr_scene_tree* top = nullptr;
        wlr_scene_tree* fullscreen = nullptr;
        wlr_scene_tree* unmanaged = nullptr; // X11 menus and tooltips
        wlr_scene_tree* overlay = nullptr;
    };

    wl_display* display = nullptr;
    wlr_backend* backend = nullptr;
    wlr_session* session = nullptr; // null for headless/nested backends
    wlr_renderer* renderer = nullptr;
    wlr_allocator* allocator = nullptr;
    wlr_compositor* compositor = nullptr;
    wlr_output_layout* output_layout = nullptr;
    wlr_scene* scene = nullptr;
    wlr_scene_output_layout* scene_layout = nullptr;
    Layers layers;
    wlr_xdg_shell* xdg_shell = nullptr;
    wlr_layer_shell_v1* layer_shell = nullptr;
    wlr_xwayland* xwayland = nullptr;
    wlr_cursor* cursor = nullptr;
    wlr_xcursor_manager* cursor_manager = nullptr;
    wlr_seat* seat = nullptr;

private:
    explicit Server(Options options);
    std::expected<void, std::string> init();
    void init_protocols();
    void init_input();
    void init_xwayland();

    // config (server.cpp)
    void load_config();
    void reload_config();
    void apply_config(const config::Config& previous);
    void watch_config();

    // processes (server.cpp)
    pid_t spawn_argv(const std::vector<std::string>& argv);
    // `program` from the same build or install as this compositor (bin/,
    // or src/<dir>/ in a build tree), else as given (found in PATH).
    [[nodiscard]] static std::string locate(const std::string& program, const std::string& build_dir);
    void start_shell();
    void reap_children();

    // events
    void handle_new_output(wlr_output* wlr_output);
    void handle_new_input(wlr_input_device* device);
    void handle_new_layer_surface(wlr_layer_surface_v1* layer_surface);
    void handle_new_popup(wlr_xdg_popup* popup);
    void update_seat_capabilities();
    void update_layer_focus();

    // pointer (input.cpp)
    struct Hit {
        wlr_surface* surface = nullptr;
        double sx = 0;
        double sy = 0;
        View* view = nullptr;
        LayerSurface* layer = nullptr;
        Decoration::Hit decoration;
    };
    Hit hit_test(double lx, double ly);
    void handle_cursor_motion(uint32_t time_msec);
    // Sends coalesced pointer motion (if any) to the client under the cursor.
    void flush_pointer_motion();
    // Re-targets pointer focus once the surface stack changed under a still
    // cursor (a popup mapped over it, a window went away). Runs when idle.
    void schedule_pointer_refocus();
    [[nodiscard]] std::chrono::microseconds motion_interval() const;
    void handle_cursor_button(const wlr_pointer_button_event& event);
    void handle_decoration_press(View& view, const Decoration::Hit& hit, uint32_t time_msec);
    void set_hovered_decoration(View* view, Decoration::Part part);
    void set_cursor_image(const char* name);
    void process_move();
    void process_resize();
    void end_grab();

    enum class GrabMode { None, PendingMove, Move, Resize, Button };
    struct Grab {
        GrabMode mode = GrabMode::None;
        View* view = nullptr;
        double x = 0; // cursor offset (move/resize) or press position (pending move)
        double y = 0;
        wlr_box box{};
        uint32_t edges = 0;
        Decoration::Part button = Decoration::Part::None;
    };
    std::optional<wlr_box> zone_target_; // where the moved window drops (Shift held)
    // Shows the zones under the pointer while moving with Shift held.
    void update_zone_preview();
    // Super+Left/Right: the previous or next zone, across displays.
    void move_to_zone(View& view, int direction);
    std::unique_ptr<ZoneOverlay> zone_overlay_;

    // Which client may see which global (shell and Xwayland-only globals).
    static bool filter_global(const wl_client* client, const wl_global* global, void* data);

    Options options_;
    config::Config config_;
    theme::Palette palette_;
    DesktopDatabase desktop_entries_;
    Taskbar taskbar_{*this};
    std::unique_ptr<ShellProtocol> shell_protocol_;
    std::unique_ptr<SessionLock> session_lock_;
    wlr_idle_notifier_v1* idle_notifier_ = nullptr;
    struct IdleInhibitor {
        Listener destroy;
    };
    std::vector<std::unique_ptr<IdleInhibitor>> idle_inhibitors_;
    void update_idle_inhibit();
    std::optional<FileWatcher> config_watcher_;
    wl_event_source* config_source_ = nullptr;

    // Window memory (placement.cpp).
    void load_window_memory();
    void reload_window_memory();
    void save_window_memory();
    void schedule_display_setup_check();
    void check_display_setup();
    windows::Memory window_memory_;
    std::filesystem::path window_memory_file_;
    std::string window_memory_text_; // the file as last read or written
    std::optional<FileWatcher> window_memory_watcher_;
    wl_event_source* window_memory_source_ = nullptr;
    wl_event_source* window_memory_timer_ = nullptr;
    wl_event_source* setup_check_idle_ = nullptr;
    std::string display_setup_; // windows::setup_key of the displays in the layout

    std::vector<std::unique_ptr<Output>> outputs_;
    // Displays as clients (Settings, wlr-randr) see them; read-only, since
    // config.json decides the layout.
    wlr_output_manager_v1* output_manager_ = nullptr;
    wl_event_source* output_manager_idle_ = nullptr;
    void update_output_manager();
    bool arranging_outputs_ = false; // applying "outputs": windows are fitted after
    std::vector<std::unique_ptr<Keyboard>> keyboards_;
    std::vector<std::unique_ptr<Pointer>> pointers_; // mice, touchpads
    std::vector<std::unique_ptr<View>> views_;
    std::vector<View*> focus_order_; // mapped views, most recently focused first
    View* focused_view_ = nullptr;
    LayerSurface* focused_layer_ = nullptr;   // layer surface given focus on click
    LayerSurface* exclusive_layer_ = nullptr; // layer surface demanding all keyboard input
    View* hovered_view_ = nullptr;            // view whose decoration is hovered
    const char* cursor_image_ = nullptr;      // compositor-set cursor, null if client-set
    Grab grab_;
    View* last_title_click_view_ = nullptr;
    uint32_t last_title_click_time_ = 0;
    bool consumed_press_ = false; // last press went to a decoration, not a client

    // Pointer motion coalescing (see handle_cursor_motion).
    wl_event_source* motion_timer_ = nullptr;
    wl_event_source* refocus_idle_ = nullptr;
    bool motion_pending_ = false;
    uint32_t pending_motion_time_ = 0;
    std::chrono::steady_clock::time_point last_motion_flush_;
    bool pointer_frame_pending_ = false; // button/axis sent since the last frame

    // TETRA_STATS=1: one line per output and second (see log_stats).
    void log_stats();
    wl_event_source* stats_timer_ = nullptr;
    int stats_pointer_events_ = 0;
    int stats_motions_sent_ = 0;

    pid_t shell_pid_ = -1;
    std::chrono::steady_clock::time_point shell_started_;
    int shell_fast_exits_ = 0;
    wl_event_source* shell_timer_ = nullptr;
    bool terminating_ = false;
    std::vector<wl_event_source*> signal_sources_;

    Listener new_output_;
    Listener new_input_;
    Listener new_toplevel_;
    Listener new_popup_;
    Listener new_layer_surface_;
    Listener new_xdg_decoration_;
    Listener new_virtual_pointer_;
    Listener new_virtual_keyboard_;
    Listener request_cursor_shape_;
    Listener xwayland_ready_;
    Listener xwayland_new_surface_;
    Listener cursor_motion_;
    Listener cursor_motion_absolute_;
    Listener cursor_button_;
    Listener cursor_axis_;
    Listener cursor_frame_;
    Listener request_cursor_;
    Listener pointer_focus_change_;
    Listener request_set_selection_;
    Listener request_set_primary_selection_;
    Listener new_idle_inhibitor_;
    Listener output_power_set_mode_;
    Listener output_manager_apply_;
    Listener output_manager_test_;
    Listener layout_change_;
};

} // namespace tetra
