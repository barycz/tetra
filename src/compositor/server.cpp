#include "server.hpp"

#include "keyboard.hpp"
#include "log.hpp"
#include "output.hpp"
#include "toplevel.hpp"

#include <algorithm>
#include <charconv>
#include <csignal>
#include <cstdlib>
#include <string_view>
#include <utility>

#include <sys/wait.h>
#include <unistd.h>

namespace tetra {

namespace {

constexpr int kCompositorVersion = 6;
constexpr int kXdgShellVersion = 3;
constexpr uint32_t kCursorSize = 24;

// Output size for backends without modes (headless, nested), e.g. "1920x1080".
std::optional<std::pair<int, int>> requested_output_size() {
    const char* env = std::getenv("TETRA_OUTPUT_SIZE"); // NOLINT(concurrency-mt-unsafe)
    if (env == nullptr) {
        return std::nullopt;
    }
    const std::string_view text{env};
    const auto sep = text.find('x');
    if (sep == std::string_view::npos) {
        return std::nullopt;
    }
    int width = 0;
    int height = 0;
    const auto w = std::from_chars(text.data(), text.data() + sep, width);
    const auto h = std::from_chars(text.data() + sep + 1, text.data() + text.size(), height);
    if (w.ec != std::errc{} || h.ec != std::errc{} || width <= 0 || height <= 0) {
        log::error("ignoring malformed TETRA_OUTPUT_SIZE='{}'", text);
        return std::nullopt;
    }
    return std::pair{width, height};
}

// Lives as long as its xdg_popup; deletes itself on destroy.
class Popup {
public:
    explicit Popup(wlr_xdg_popup* popup) : popup_(popup) {
        commit_.connect(popup_->base->surface->events.commit, [this] {
            if (popup_->base->initial_commit) {
                wlr_xdg_surface_schedule_configure(popup_->base);
            }
        });
        destroy_.connect(popup_->events.destroy, [this] { delete this; });
    }

    Popup(const Popup&) = delete;
    Popup& operator=(const Popup&) = delete;
    Popup(Popup&&) = delete;
    Popup& operator=(Popup&&) = delete;

private:
    ~Popup() = default;

    wlr_xdg_popup* popup_;
    Listener commit_;
    Listener destroy_;
};

int reap_children(int /*signal*/, void* /*data*/) {
    while (waitpid(-1, nullptr, WNOHANG) > 0) {
    }
    return 0;
}

int terminate_display(int /*signal*/, void* data) {
    wl_display_terminate(static_cast<wl_display*>(data));
    return 0;
}

} // namespace

std::expected<std::unique_ptr<Server>, std::string> Server::create(Options options) {
    std::unique_ptr<Server> server{new Server(std::move(options))};
    if (auto result = server->init(); !result) {
        return std::unexpected(std::move(result.error()));
    }
    return server;
}

Server::Server(Options options) : options_(std::move(options)) {}

Server::~Server() {
    // Order matters: wlroots asserts that no listeners remain on objects it
    // destroys, so tear down our side first, then wlroots in reverse order.
    if (display != nullptr) {
        wl_display_destroy_clients(display);
    }
    focus_order_.clear();
    toplevels_.clear();
    keyboards_.clear();
    outputs_.clear();

    for (Listener* listener :
         {&new_output_, &new_input_, &new_toplevel_, &new_popup_, &cursor_motion_,
          &cursor_motion_absolute_, &cursor_button_, &cursor_axis_, &cursor_frame_,
          &request_cursor_, &pointer_focus_change_, &request_set_selection_,
          &request_set_primary_selection_}) {
        listener->disconnect();
    }
    for (wl_event_source* source : signal_sources_) {
        wl_event_source_remove(source);
    }

    if (scene != nullptr) {
        wlr_scene_node_destroy(&scene->tree.node);
    }
    if (cursor_manager != nullptr) {
        wlr_xcursor_manager_destroy(cursor_manager);
    }
    if (cursor != nullptr) {
        wlr_cursor_destroy(cursor);
    }
    if (allocator != nullptr) {
        wlr_allocator_destroy(allocator);
    }
    if (renderer != nullptr) {
        wlr_renderer_destroy(renderer);
    }
    if (backend != nullptr) {
        wlr_backend_destroy(backend);
    }
    if (display != nullptr) {
        wl_display_destroy(display);
    }
}

std::expected<void, std::string> Server::init() {
    display = wl_display_create();
    if (display == nullptr) {
        return std::unexpected("failed to create Wayland display");
    }
    wl_event_loop* loop = wl_display_get_event_loop(display);

    // Picks DRM/libinput on a TTY, nested Wayland/X11 inside another session,
    // or whatever WLR_BACKENDS says (e.g. headless).
    backend = wlr_backend_autocreate(loop, &session);
    if (backend == nullptr) {
        return std::unexpected("failed to create wlroots backend");
    }
    renderer = wlr_renderer_autocreate(backend);
    if (renderer == nullptr) {
        return std::unexpected("failed to create renderer");
    }
    wlr_renderer_init_wl_display(renderer, display);
    allocator = wlr_allocator_autocreate(backend, renderer);
    if (allocator == nullptr) {
        return std::unexpected("failed to create allocator");
    }

    wlr_compositor_create(display, kCompositorVersion, renderer);
    wlr_subcompositor_create(display);
    wlr_data_device_manager_create(display);
    wlr_primary_selection_v1_device_manager_create(display);
    wlr_viewporter_create(display);
    wlr_screencopy_manager_v1_create(display); // screenshots (grim)

    output_layout = wlr_output_layout_create(display);
    wlr_xdg_output_manager_v1_create(display, output_layout);
    new_output_.connect<wlr_output>(backend->events.new_output,
                                    [this](wlr_output* output) { handle_new_output(output); });

    scene = wlr_scene_create();
    scene_layout = wlr_scene_attach_output_layout(scene, output_layout);
    layers.background = wlr_scene_tree_create(&scene->tree);
    layers.windows = wlr_scene_tree_create(&scene->tree);
    layers.overlay = wlr_scene_tree_create(&scene->tree);

    xdg_shell = wlr_xdg_shell_create(display, kXdgShellVersion);
    new_toplevel_.connect<wlr_xdg_toplevel>(
        xdg_shell->events.new_toplevel,
        [this](wlr_xdg_toplevel* toplevel) { handle_new_toplevel(toplevel); });
    new_popup_.connect<wlr_xdg_popup>(xdg_shell->events.new_popup,
                                      [this](wlr_xdg_popup* popup) { handle_new_popup(popup); });

    init_input();

    signal_sources_.push_back(wl_event_loop_add_signal(loop, SIGCHLD, reap_children, nullptr));
    signal_sources_.push_back(wl_event_loop_add_signal(loop, SIGINT, terminate_display, display));
    signal_sources_.push_back(wl_event_loop_add_signal(loop, SIGTERM, terminate_display, display));
    std::erase(signal_sources_, nullptr);
    return {};
}

void Server::init_input() {
    cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(cursor, output_layout);
    cursor_manager = wlr_xcursor_manager_create(nullptr, kCursorSize);

    cursor_motion_.connect<wlr_pointer_motion_event>(cursor->events.motion, [this](auto* event) {
        wlr_cursor_move(cursor, &event->pointer->base, event->delta_x, event->delta_y);
        process_cursor_motion(event->time_msec);
    });
    cursor_motion_absolute_.connect<wlr_pointer_motion_absolute_event>(
        cursor->events.motion_absolute, [this](auto* event) {
            wlr_cursor_warp_absolute(cursor, &event->pointer->base, event->x, event->y);
            process_cursor_motion(event->time_msec);
        });
    cursor_button_.connect<wlr_pointer_button_event>(
        cursor->events.button, [this](auto* event) { handle_cursor_button(*event); });
    cursor_axis_.connect<wlr_pointer_axis_event>(cursor->events.axis, [this](auto* event) {
        wlr_seat_pointer_notify_axis(seat, event->time_msec, event->orientation, event->delta,
                                     event->delta_discrete, event->source,
                                     event->relative_direction);
    });
    cursor_frame_.connect(cursor->events.frame, [this] { wlr_seat_pointer_notify_frame(seat); });

    new_input_.connect<wlr_input_device>(
        backend->events.new_input, [this](wlr_input_device* device) { handle_new_input(device); });

    seat = wlr_seat_create(display, "seat0");
    request_cursor_.connect<wlr_seat_pointer_request_set_cursor_event>(
        seat->events.request_set_cursor, [this](auto* event) {
            // Any client may ask; only the one with pointer focus gets its way.
            if (event->seat_client == seat->pointer_state.focused_client) {
                wlr_cursor_set_surface(cursor, event->surface, event->hotspot_x, event->hotspot_y);
            }
        });
    pointer_focus_change_.connect<wlr_seat_pointer_focus_change_event>(
        seat->pointer_state.events.focus_change, [this](auto* event) {
            if (event->new_surface == nullptr) {
                wlr_cursor_set_xcursor(cursor, cursor_manager, "default");
            }
        });
    request_set_selection_.connect<wlr_seat_request_set_selection_event>(
        seat->events.request_set_selection,
        [this](auto* event) { wlr_seat_set_selection(seat, event->source, event->serial); });
    request_set_primary_selection_.connect<wlr_seat_request_set_primary_selection_event>(
        seat->events.request_set_primary_selection, [this](auto* event) {
            wlr_seat_set_primary_selection(seat, event->source, event->serial);
        });
}

std::expected<void, std::string> Server::run() {
    const char* socket = wl_display_add_socket_auto(display);
    if (socket == nullptr) {
        return std::unexpected("failed to open a Wayland socket");
    }
    if (!wlr_backend_start(backend)) {
        return std::unexpected("failed to start backend");
    }

    setenv("WAYLAND_DISPLAY", socket, 1); // NOLINT(concurrency-mt-unsafe)
    log::info("running on WAYLAND_DISPLAY={}", socket);
    if (options_.startup_command) {
        spawn(*options_.startup_command);
    }

    wl_display_run(display);
    return {};
}

void Server::terminate() {
    wl_display_terminate(display);
}

void Server::spawn(const std::string& command) {
    const pid_t pid = fork();
    if (pid < 0) {
        log::error("fork failed: {}", std::strerror(errno)); // NOLINT(concurrency-mt-unsafe)
        return;
    }
    if (pid == 0) {
        // libwayland blocks the signals it handles via signalfd; the mask
        // survives exec, so restore it before starting the client.
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        setsid();
        execl("/bin/sh", "/bin/sh", "-c", command.c_str(), nullptr);
        _exit(127);
    }
    log::debug("spawned '{}' (pid {})", command, pid);
}

// --- outputs and input devices ---------------------------------------------

void Server::handle_new_output(wlr_output* wlr_output) {
    wlr_output_init_render(wlr_output, allocator, renderer);

    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    if (wlr_output_mode* mode = wlr_output_preferred_mode(wlr_output)) {
        wlr_output_state_set_mode(&state, mode);
    } else if (const auto size = requested_output_size()) {
        wlr_output_state_set_custom_mode(&state, size->first, size->second, 0);
    }
    if (!wlr_output_commit_state(wlr_output, &state)) {
        log::error("failed to enable output {}", wlr_output->name);
    }
    wlr_output_state_finish(&state);

    outputs_.push_back(std::make_unique<Output>(*this, wlr_output));
    log::info("output {} added ({}x{})", wlr_output->name, wlr_output->width,
              wlr_output->height);
}

void Server::destroy_output(Output& output) {
    std::erase_if(outputs_, [&](const auto& ptr) { return ptr.get() == &output; });
}

void Server::handle_new_input(wlr_input_device* device) {
    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        keyboards_.push_back(std::make_unique<Keyboard>(*this, device));
        break;
    case WLR_INPUT_DEVICE_POINTER:
        wlr_cursor_attach_input_device(cursor, device);
        break;
    default:
        break;
    }
    update_seat_capabilities();
}

void Server::destroy_keyboard(Keyboard& keyboard) {
    std::erase_if(keyboards_, [&](const auto& ptr) { return ptr.get() == &keyboard; });
    update_seat_capabilities();
}

void Server::update_seat_capabilities() {
    // We always draw a cursor, so always advertise a pointer.
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!keyboards_.empty()) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    wlr_seat_set_capabilities(seat, caps);
}

bool Server::handle_keybinding(uint32_t modifiers, xkb_keysym_t sym) {
    // Ctrl+Alt+F<n> arrives as XF86Switch_VT_<n>.
    if (sym >= XKB_KEY_XF86Switch_VT_1 && sym <= XKB_KEY_XF86Switch_VT_12) {
        if (session != nullptr) {
            wlr_session_change_vt(session, sym - XKB_KEY_XF86Switch_VT_1 + 1);
        }
        return true;
    }

    // Development bindings, to be replaced by the config-driven keymap.
    const bool super = (modifiers & WLR_MODIFIER_LOGO) != 0;
    const bool shift = (modifiers & WLR_MODIFIER_SHIFT) != 0;
    const bool alt = (modifiers & WLR_MODIFIER_ALT) != 0;
    if (super && shift && sym == XKB_KEY_Escape) {
        terminate();
        return true;
    }
    if (super && sym == XKB_KEY_Return) {
        spawn("foot");
        return true;
    }
    if (alt && sym == XKB_KEY_Tab) {
        focus_next();
        return true;
    }
    return false;
}

// --- toplevels ----------------------------------------------------------------

void Server::handle_new_toplevel(wlr_xdg_toplevel* xdg_toplevel) {
    toplevels_.push_back(std::make_unique<Toplevel>(*this, xdg_toplevel));
}

void Server::handle_new_popup(wlr_xdg_popup* xdg_popup) {
    // Popups are rendered as part of their parent's scene tree.
    wlr_xdg_surface* parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    if (parent != nullptr && parent->data != nullptr) {
        xdg_popup->base->data =
            wlr_scene_xdg_surface_create(static_cast<wlr_scene_tree*>(parent->data), xdg_popup->base);
    } else {
        log::error("popup without a known parent; it will not be shown");
    }
    new Popup(xdg_popup); // NOLINT(*-owning-memory): owned by its destroy signal
}

void Server::destroy_toplevel(Toplevel& toplevel) {
    std::erase(focus_order_, &toplevel);
    if (grab_.toplevel == &toplevel) {
        grab_ = {};
    }
    std::erase_if(toplevels_, [&](const auto& ptr) { return ptr.get() == &toplevel; });
}

void Server::handle_toplevel_mapped(Toplevel& toplevel) {
    focus(&toplevel);
}

void Server::handle_toplevel_unmapped(Toplevel& toplevel) {
    if (grab_.toplevel == &toplevel) {
        grab_ = {};
    }
    const bool was_focused = seat->keyboard_state.focused_surface == toplevel.surface();
    std::erase(focus_order_, &toplevel);
    if (was_focused) {
        if (focus_order_.empty()) {
            wlr_seat_keyboard_notify_clear_focus(seat);
        } else {
            focus(focus_order_.front());
        }
    }
}

void Server::focus(Toplevel* toplevel) {
    if (toplevel == nullptr) {
        return;
    }
    wlr_surface* previous = seat->keyboard_state.focused_surface;
    wlr_surface* surface = toplevel->surface();
    if (previous == surface) {
        return;
    }
    if (previous != nullptr) {
        if (wlr_xdg_toplevel* prev = wlr_xdg_toplevel_try_from_wlr_surface(previous)) {
            wlr_xdg_toplevel_set_activated(prev, false);
        }
    }

    toplevel->raise();
    std::erase(focus_order_, toplevel);
    focus_order_.insert(focus_order_.begin(), toplevel);
    toplevel->set_activated(true);

    if (wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat)) {
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes, keyboard->num_keycodes,
                                       &keyboard->modifiers);
    } else {
        wlr_seat_keyboard_notify_enter(seat, surface, nullptr, 0, nullptr);
    }
}

void Server::focus_next() {
    if (focus_order_.size() >= 2) {
        focus(focus_order_.back());
    }
}

// --- pointer ------------------------------------------------------------------

Toplevel* Server::toplevel_at(double lx, double ly, wlr_surface*& surface, double& sx,
                              double& sy) {
    wlr_scene_node* node = wlr_scene_node_at(&layers.windows->node, lx, ly, &sx, &sy);
    if (node == nullptr || node->type != WLR_SCENE_NODE_BUFFER) {
        return nullptr;
    }
    wlr_scene_surface* scene_surface =
        wlr_scene_surface_try_from_buffer(wlr_scene_buffer_from_node(node));
    if (scene_surface == nullptr) {
        return nullptr;
    }
    surface = scene_surface->surface;
    for (wlr_scene_tree* tree = node->parent; tree != nullptr; tree = tree->node.parent) {
        if (tree->node.data != nullptr) {
            return static_cast<Toplevel*>(tree->node.data);
        }
    }
    return nullptr;
}

void Server::process_cursor_motion(uint32_t time_msec) {
    switch (grab_.mode) {
    case CursorMode::Move:
        process_move();
        return;
    case CursorMode::Resize:
        process_resize();
        return;
    case CursorMode::Passthrough:
        break;
    }

    double sx = 0;
    double sy = 0;
    wlr_surface* surface = nullptr;
    if (toplevel_at(cursor->x, cursor->y, surface, sx, sy) == nullptr) {
        wlr_cursor_set_xcursor(cursor, cursor_manager, "default");
    }
    if (surface != nullptr) {
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, time_msec, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(seat);
    }
}

void Server::handle_cursor_button(const wlr_pointer_button_event& event) {
    wlr_seat_pointer_notify_button(seat, event.time_msec, event.button, event.state);
    if (event.state == WL_POINTER_BUTTON_STATE_RELEASED) {
        grab_ = {};
        return;
    }
    double sx = 0;
    double sy = 0;
    wlr_surface* surface = nullptr;
    focus(toplevel_at(cursor->x, cursor->y, surface, sx, sy));
}

void Server::begin_interactive_move(Toplevel& toplevel) {
    grab_ = {.mode = CursorMode::Move,
             .toplevel = &toplevel,
             .x = cursor->x - toplevel.x(),
             .y = cursor->y - toplevel.y()};
}

void Server::begin_interactive_resize(Toplevel& toplevel, uint32_t edges) {
    const wlr_box& geo = toplevel.geometry();
    const double border_x =
        toplevel.x() + geo.x + ((edges & WLR_EDGE_RIGHT) != 0 ? geo.width : 0);
    const double border_y =
        toplevel.y() + geo.y + ((edges & WLR_EDGE_BOTTOM) != 0 ? geo.height : 0);

    grab_ = {.mode = CursorMode::Resize,
             .toplevel = &toplevel,
             .x = cursor->x - border_x,
             .y = cursor->y - border_y,
             .box = {.x = geo.x + toplevel.x(),
                     .y = geo.y + toplevel.y(),
                     .width = geo.width,
                     .height = geo.height},
             .edges = edges};
}

void Server::process_move() {
    grab_.toplevel->set_position(static_cast<int>(cursor->x - grab_.x),
                                 static_cast<int>(cursor->y - grab_.y));
}

void Server::process_resize() {
    // Simplification: position and size change immediately instead of waiting
    // for the client to commit a buffer at the new size.
    const auto border_x = static_cast<int>(cursor->x - grab_.x);
    const auto border_y = static_cast<int>(cursor->y - grab_.y);
    int left = grab_.box.x;
    int right = grab_.box.x + grab_.box.width;
    int top = grab_.box.y;
    int bottom = grab_.box.y + grab_.box.height;

    if ((grab_.edges & WLR_EDGE_TOP) != 0) {
        top = std::min(border_y, bottom - 1);
    } else if ((grab_.edges & WLR_EDGE_BOTTOM) != 0) {
        bottom = std::max(border_y, top + 1);
    }
    if ((grab_.edges & WLR_EDGE_LEFT) != 0) {
        left = std::min(border_x, right - 1);
    } else if ((grab_.edges & WLR_EDGE_RIGHT) != 0) {
        right = std::max(border_x, left + 1);
    }

    Toplevel& toplevel = *grab_.toplevel;
    const wlr_box& geo = toplevel.geometry();
    toplevel.set_position(left - geo.x, top - geo.y);
    wlr_xdg_toplevel_set_size(toplevel.xdg(), right - left, bottom - top);
}

} // namespace tetra
