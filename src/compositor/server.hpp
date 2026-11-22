#pragma once

#include "listener.hpp"
#include "wlr.hpp"

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tetra {

class Keyboard;
class Output;
class Toplevel;

struct Options {
    std::optional<std::string> startup_command;
};

// Owns the Wayland display, the wlroots objects and every compositor-side
// object (outputs, keyboards, toplevels). One instance per process.
class Server {
public:
    static std::expected<std::unique_ptr<Server>, std::string> create(Options options);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;

    // Opens the socket, starts the backend, launches the startup command and
    // runs the event loop until terminate().
    std::expected<void, std::string> run();
    void terminate();
    void spawn(const std::string& command);

    // Window management, called by Toplevel / Keyboard.
    void focus(Toplevel* toplevel);
    void focus_next();
    void begin_interactive_move(Toplevel& toplevel);
    void begin_interactive_resize(Toplevel& toplevel, uint32_t edges);
    void handle_toplevel_mapped(Toplevel& toplevel);
    void handle_toplevel_unmapped(Toplevel& toplevel);
    bool handle_keybinding(uint32_t modifiers, xkb_keysym_t sym);

    // Ownership: called from the objects' own destroy handlers.
    void destroy_output(Output& output);
    void destroy_keyboard(Keyboard& keyboard);
    void destroy_toplevel(Toplevel& toplevel);

    // Scene tree layers, bottom to top.
    struct Layers {
        wlr_scene_tree* background = nullptr;
        wlr_scene_tree* windows = nullptr;
        wlr_scene_tree* overlay = nullptr;
    };

    wl_display* display = nullptr;
    wlr_backend* backend = nullptr;
    wlr_session* session = nullptr; // null for headless/nested backends
    wlr_renderer* renderer = nullptr;
    wlr_allocator* allocator = nullptr;
    wlr_output_layout* output_layout = nullptr;
    wlr_scene* scene = nullptr;
    wlr_scene_output_layout* scene_layout = nullptr;
    Layers layers;
    wlr_xdg_shell* xdg_shell = nullptr;
    wlr_cursor* cursor = nullptr;
    wlr_xcursor_manager* cursor_manager = nullptr;
    wlr_seat* seat = nullptr;

private:
    explicit Server(Options options);
    std::expected<void, std::string> init();
    void init_input();

    void handle_new_output(wlr_output* wlr_output);
    void handle_new_input(wlr_input_device* device);
    void handle_new_toplevel(wlr_xdg_toplevel* xdg_toplevel);
    void handle_new_popup(wlr_xdg_popup* xdg_popup);
    void handle_cursor_button(const wlr_pointer_button_event& event);
    void process_cursor_motion(uint32_t time_msec);
    void process_move();
    void process_resize();
    void update_seat_capabilities();

    // Topmost toplevel under layout coordinates (lx, ly), plus the surface and
    // surface-local coordinates hit. Returns nullptr over the desktop.
    Toplevel* toplevel_at(double lx, double ly, wlr_surface*& surface, double& sx, double& sy);

    enum class CursorMode { Passthrough, Move, Resize };
    struct Grab {
        CursorMode mode = CursorMode::Passthrough;
        Toplevel* toplevel = nullptr;
        double x = 0;
        double y = 0;
        wlr_box box{};
        uint32_t edges = 0;
    };

    Options options_;
    std::vector<std::unique_ptr<Output>> outputs_;
    std::vector<std::unique_ptr<Keyboard>> keyboards_;
    std::vector<std::unique_ptr<Toplevel>> toplevels_;
    std::vector<Toplevel*> focus_order_; // mapped toplevels, most recent first
    Grab grab_;
    std::vector<wl_event_source*> signal_sources_;

    Listener new_output_;
    Listener new_input_;
    Listener new_toplevel_;
    Listener new_popup_;
    Listener cursor_motion_;
    Listener cursor_motion_absolute_;
    Listener cursor_button_;
    Listener cursor_axis_;
    Listener cursor_frame_;
    Listener request_cursor_;
    Listener pointer_focus_change_;
    Listener request_set_selection_;
    Listener request_set_primary_selection_;
};

} // namespace tetra
