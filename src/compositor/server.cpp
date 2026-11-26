// Server setup and teardown, configuration, child processes.

#include "server.hpp"

#include "keyboard.hpp"
#include "layer_surface.hpp"
#include "log.hpp"
#include "output.hpp"
#include "pointer.hpp"
#include "popup.hpp"
#include "session_lock.hpp"
#include "shell_protocol.hpp"
#include "view.hpp"
#include "xdg_view.hpp"
#include "xwayland_view.hpp"

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <utility>

#include <sys/wait.h>
#include <unistd.h>

namespace tetra {

namespace {

constexpr int kCompositorVersion = 6;
constexpr int kXdgShellVersion = 3;
constexpr int kLayerShellVersion = 4;
constexpr auto kShellFastExit = std::chrono::seconds(10);
constexpr int kShellMaxFastExits = 5;

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
    terminating_ = true;
    session_lock_.reset(); // its listeners must not run while clients go away
    idle_inhibitors_.clear();
    if (display != nullptr) {
        wl_display_destroy_clients(display);
    }
    if (xwayland != nullptr) {
        xwayland_ready_.disconnect();
        xwayland_new_surface_.disconnect();
        wlr_xwayland_destroy(xwayland);
        xwayland = nullptr;
    }
    focus_order_.clear();
    focused_view_ = nullptr;
    hovered_view_ = nullptr;
    grab_ = {};
    zone_overlay_.reset();
    views_.clear();
    save_window_memory(); // what the closing windows remembered
    keyboards_.clear();
    pointers_.clear();
    outputs_.clear();
    taskbar_.on_changed = nullptr;
    shell_protocol_.reset();

    for (Listener* listener :
         {&new_output_, &new_input_, &new_toplevel_, &new_popup_, &new_layer_surface_,
          &new_xdg_decoration_, &new_virtual_pointer_, &new_virtual_keyboard_,
          &request_cursor_shape_, &cursor_motion_, &cursor_motion_absolute_, &cursor_button_,
          &cursor_axis_, &cursor_frame_, &request_cursor_, &pointer_focus_change_,
          &request_set_selection_, &request_set_primary_selection_, &new_idle_inhibitor_,
          &output_power_set_mode_, &output_manager_apply_, &output_manager_test_, &layout_change_}) {
        listener->disconnect();
    }
    if (output_manager_idle_ != nullptr) {
        wl_event_source_remove(output_manager_idle_);
    }
    for (wl_event_source* source : signal_sources_) {
        wl_event_source_remove(source);
    }
    if (config_source_ != nullptr) {
        wl_event_source_remove(config_source_);
    }
    for (wl_event_source* source : {window_memory_source_, window_memory_timer_, setup_check_idle_}) {
        if (source != nullptr) {
            wl_event_source_remove(source);
        }
    }
    if (shell_timer_ != nullptr) {
        wl_event_source_remove(shell_timer_);
    }
    if (motion_timer_ != nullptr) {
        wl_event_source_remove(motion_timer_);
    }
    if (refocus_idle_ != nullptr) {
        wl_event_source_remove(refocus_idle_);
    }
    if (stats_timer_ != nullptr) {
        wl_event_source_remove(stats_timer_);
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
    load_config();

    display = wl_display_create();
    if (display == nullptr) {
        return std::unexpected("failed to create Wayland display");
    }
    wl_event_loop* loop = wl_display_get_event_loop(display);

    // DRM/libinput on a TTY, nested inside another session, or whatever
    // WLR_BACKENDS says (e.g. headless).
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

    compositor = wlr_compositor_create(display, kCompositorVersion, renderer);
    output_layout = wlr_output_layout_create(display);

    scene = wlr_scene_create();
    scene_layout = wlr_scene_attach_output_layout(scene, output_layout);
    layers.background = wlr_scene_tree_create(&scene->tree);
    layers.bottom = wlr_scene_tree_create(&scene->tree);
    layers.windows = wlr_scene_tree_create(&scene->tree);
    layers.zones = wlr_scene_tree_create(&scene->tree);
    layers.top = wlr_scene_tree_create(&scene->tree);
    layers.fullscreen = wlr_scene_tree_create(&scene->tree);
    layers.unmanaged = wlr_scene_tree_create(&scene->tree);
    layers.overlay = wlr_scene_tree_create(&scene->tree);
    zone_overlay_ = std::make_unique<ZoneOverlay>(*this, layers.zones);
    session_lock_ = std::make_unique<SessionLock>(*this); // its cover goes above everything
    layout_change_.connect(output_layout->events.change, [this] {
        if (session_lock_) {
            session_lock_->handle_layout_changed();
        }
        schedule_output_manager_update();
        schedule_display_setup_check();
    });

    init_protocols();
    init_input();
    init_xwayland();

    desktop_entries_.load();
    shell_protocol_ = std::make_unique<ShellProtocol>(*this);
    taskbar_.on_changed = [this](const Taskbar::Snapshot& snapshot) {
        shell_protocol_->publish(snapshot);
    };
    taskbar_.set_pins(config_.taskbar.pinned);
    wl_display_set_global_filter(display, &Server::filter_global, this);

    new_output_.connect<wlr_output>(backend->events.new_output,
                                    [this](wlr_output* output) { handle_new_output(output); });

    auto on_signal = [](int signal, void* data) {
        auto* self = static_cast<Server*>(data);
        if (signal == SIGCHLD) {
            self->reap_children();
        } else {
            self->terminate();
        }
        return 0;
    };
    for (const int signal : {SIGCHLD, SIGINT, SIGTERM}) {
        if (wl_event_source* source = wl_event_loop_add_signal(loop, signal, on_signal, this)) {
            signal_sources_.push_back(source);
        }
    }
    shell_timer_ = wl_event_loop_add_timer(
        loop,
        [](void* data) {
            static_cast<Server*>(data)->start_shell();
            return 0;
        },
        this);

    if (const char* stats = std::getenv("TETRA_STATS"); stats != nullptr && *stats == '1') { // NOLINT
        stats_timer_ = wl_event_loop_add_timer(
            loop,
            [](void* data) {
                static_cast<Server*>(data)->log_stats();
                return 0;
            },
            this);
        wl_event_source_timer_update(stats_timer_, 1000);
    }

    watch_config();
    load_window_memory();
    return {};
}

void Server::log_stats() {
    for (const auto& output : outputs_) {
        log::info("stats {}", output->take_stats());
    }
    log::info("stats pointer: {} events in, {} motions sent to clients", stats_pointer_events_,
              stats_motions_sent_);
    stats_pointer_events_ = 0;
    stats_motions_sent_ = 0;
    wl_event_source_timer_update(stats_timer_, 1000);
}

void Server::init_protocols() {
    wlr_subcompositor_create(display);
    wlr_data_device_manager_create(display);
    wlr_primary_selection_v1_device_manager_create(display);
    // Clipboard managers and tools like wl-copy/wl-paste work without
    // keyboard focus through these.
    wlr_data_control_manager_v1_create(display);
    wlr_ext_data_control_manager_v1_create(display, 1);
    wlr_viewporter_create(display);
    wlr_single_pixel_buffer_manager_v1_create(display);
    wlr_fractional_scale_manager_v1_create(display, 1);
    wlr_presentation_create(display, backend, 2);
    wlr_screencopy_manager_v1_create(display);
    // xdg-foreign: a client's window as the parent of another client's
    // (portal dialogs over the app that asked). wlroots sets the parent.
    wlr_xdg_foreign_registry* foreign = wlr_xdg_foreign_registry_create(display);
    wlr_xdg_foreign_v1_create(display, foreign);
    wlr_xdg_foreign_v2_create(display, foreign);
    wlr_xdg_output_manager_v1_create(display, output_layout);

    // Idle: clients (tetra-shell) get notified after a timeout without
    // input, unless an app inhibits it (video players).
    idle_notifier_ = wlr_idle_notifier_v1_create(display);
    auto* idle_inhibit = wlr_idle_inhibit_v1_create(display);
    new_idle_inhibitor_.connect<wlr_idle_inhibitor_v1>(
        idle_inhibit->events.new_inhibitor, [this](wlr_idle_inhibitor_v1* inhibitor) {
            auto entry = std::make_unique<IdleInhibitor>();
            IdleInhibitor* raw = entry.get();
            entry->destroy.connect(inhibitor->events.destroy, [this, raw] {
                std::erase_if(idle_inhibitors_, [raw](const auto& e) { return e.get() == raw; });
                update_idle_inhibit();
            });
            idle_inhibitors_.push_back(std::move(entry));
            update_idle_inhibit();
        });
    // Displays, their modes and the layout, for Settings and tools like
    // wlr-randr. Changes go through config.json, which stays the single
    // source of truth, so requests to apply (or test) one are refused.
    output_manager_ = wlr_output_manager_v1_create(display);
    auto refuse = [](wlr_output_configuration_v1* request) {
        log::info("refused a display configuration request: displays are set in config.json "
                  "(\"outputs\", or Settings > Displays)");
        wlr_output_configuration_v1_send_failed(request);
        wlr_output_configuration_v1_destroy(request);
    };
    output_manager_apply_.connect<wlr_output_configuration_v1>(output_manager_->events.apply, refuse);
    output_manager_test_.connect<wlr_output_configuration_v1>(output_manager_->events.test, refuse);

    // Screens off/on (the shell blanks them when idle).
    auto* output_power = wlr_output_power_manager_v1_create(display);
    output_power_set_mode_.connect<wlr_output_power_v1_set_mode_event>(
        output_power->events.set_mode, [this](wlr_output_power_v1_set_mode_event* event) {
            if (Output* output = find_output(event->output)) {
                output->set_power(event->mode == ZWLR_OUTPUT_POWER_V1_MODE_ON);
            }
        });

    xdg_shell = wlr_xdg_shell_create(display, kXdgShellVersion);
    new_toplevel_.connect<wlr_xdg_toplevel>(
        xdg_shell->events.new_toplevel, [this](wlr_xdg_toplevel* toplevel) {
            views_.push_back(std::make_unique<XdgView>(*this, toplevel));
        });
    new_popup_.connect<wlr_xdg_popup>(xdg_shell->events.new_popup,
                                      [this](wlr_xdg_popup* popup) { handle_new_popup(popup); });

    layer_shell = wlr_layer_shell_v1_create(display, kLayerShellVersion);
    new_layer_surface_.connect<wlr_layer_surface_v1>(
        layer_shell->events.new_surface,
        [this](wlr_layer_surface_v1* surface) { handle_new_layer_surface(surface); });

    // Ask every client that supports it to let us draw its decorations.
    wlr_xdg_decoration_manager_v1* decorations = wlr_xdg_decoration_manager_v1_create(display);
    new_xdg_decoration_.connect<wlr_xdg_toplevel_decoration_v1>(
        decorations->events.new_toplevel_decoration, [this](auto* decoration) {
            for (const auto& view : views_) {
                auto* xdg = dynamic_cast<XdgView*>(view.get());
                if (xdg != nullptr && xdg->toplevel() == decoration->toplevel) {
                    xdg->set_xdg_decoration(decoration);
                    return;
                }
            }
        });
}

void Server::init_xwayland() {
    xwayland = wlr_xwayland_create(display, compositor, /*lazy=*/true);
    if (xwayland == nullptr) {
        log::error("Xwayland is not available; X11 applications will not run");
        return;
    }
    setenv("DISPLAY", xwayland->display_name, 1); // NOLINT(concurrency-mt-unsafe)

    xwayland_ready_.connect(xwayland->events.ready, [this] {
        wlr_xwayland_set_seat(xwayland, seat);
        wlr_xcursor_manager_load(cursor_manager, 1);
        if (wlr_xcursor* xcursor = wlr_xcursor_manager_get_xcursor(cursor_manager, "default", 1)) {
            wlr_xcursor_image* image = *xcursor->images;
            wlr_xwayland_set_cursor(xwayland, wlr_xcursor_image_get_buffer(image),
                                    static_cast<int32_t>(image->hotspot_x),
                                    static_cast<int32_t>(image->hotspot_y));
        }
        log::info("Xwayland ready on DISPLAY={}", xwayland->display_name);
    });
    xwayland_new_surface_.connect<wlr_xwayland_surface>(
        xwayland->events.new_surface, [this](wlr_xwayland_surface* xsurface) {
            if (xsurface->override_redirect) {
                new XwaylandUnmanaged(*this, xsurface); // NOLINT: owned by its destroy signal
            } else {
                views_.push_back(std::make_unique<XwaylandView>(*this, xsurface));
            }
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

    setenv("WAYLAND_DISPLAY", socket, 1);              // NOLINT(concurrency-mt-unsafe)
    setenv("XDG_CURRENT_DESKTOP", "tetra", 1);         // NOLINT(concurrency-mt-unsafe)
    setenv("XDG_SESSION_TYPE", "wayland", 1);          // NOLINT(concurrency-mt-unsafe)
    log::info("running on WAYLAND_DISPLAY={}", socket);

    if (session != nullptr) {
        // Real session (not headless/nested): let D-Bus- and systemd-activated
        // services (portals, notification actions, ...) find this display.
        spawn("dbus-update-activation-environment --systemd WAYLAND_DISPLAY DISPLAY "
              "XDG_CURRENT_DESKTOP XDG_SESSION_TYPE");
    }
    start_shell();
    for (const auto& command : config_.autostart) {
        spawn(command);
    }
    if (options_.startup_command) {
        spawn(*options_.startup_command);
    }

    wl_display_run(display);
    return {};
}

void Server::terminate() {
    terminating_ = true;
    wl_display_terminate(display);
}

void Server::show_launcher() {
    if (shell_protocol_ && !locked()) {
        shell_protocol_->show_launcher();
    }
}

// --- session lock and idle ------------------------------------------------------------

void Server::request_lock() {
    if (shell_protocol_ && (!locked() || lock_abandoned())) {
        shell_protocol_->lock();
    }
}

bool Server::locked() const noexcept {
    return session_lock_ && session_lock_->active();
}

bool Server::lock_abandoned() const noexcept {
    return session_lock_ && session_lock_->abandoned();
}

void Server::handle_lock_changed() {
    if (terminating_) {
        return;
    }
    if (locked()) {
        end_grab();
        set_hovered_decoration(nullptr, Decoration::Part::None);
    }
    restore_keyboard_focus();
    schedule_pointer_refocus();
}

void Server::handle_output_frame(wlr_output* output) {
    if (session_lock_) {
        session_lock_->handle_output_frame(output);
    }
}

void Server::notify_activity() {
    if (idle_notifier_ != nullptr) {
        wlr_idle_notifier_v1_notify_activity(idle_notifier_, seat);
    }
}

void Server::update_idle_inhibit() {
    // Simplification: any inhibitor counts, visible or not.
    if (idle_notifier_ != nullptr) {
        wlr_idle_notifier_v1_set_inhibited(idle_notifier_, !idle_inhibitors_.empty());
    }
}

bool Server::filter_global(const wl_client* client, const wl_global* global, void* data) {
    const auto* self = static_cast<const Server*>(data);
    auto* mutable_client = const_cast<wl_client*>(client); // NOLINT: libwayland API
    if (self->shell_protocol_ && global == self->shell_protocol_->global()) {
        pid_t pid = 0;
        wl_client_get_credentials(mutable_client, &pid, nullptr, nullptr);
        return self->shell_pid_ > 0 && pid == self->shell_pid_;
    }
    if (self->xwayland != nullptr && self->xwayland->shell_v1 != nullptr &&
        global == self->xwayland->shell_v1->global) {
        return self->xwayland->server != nullptr && client == self->xwayland->server->client;
    }
    return true;
}

// --- configuration -------------------------------------------------------------

void Server::load_config() {
    const auto path = options_.config_path ? std::filesystem::path(*options_.config_path)
                                           : config::config_file();
    if (options_.config_path) {
        // The shell and apps read the same file.
        setenv("TETRA_CONFIG", path.c_str(), 1); // NOLINT(concurrency-mt-unsafe)
    }
    std::vector<std::string> warnings;
    if (auto loaded = config::load(path, &warnings)) {
        config_ = std::move(*loaded);
        log::info("configuration: {}", path.string());
    } else {
        log::error("{}; using defaults", loaded.error());
        config_ = {};
    }
    for (const auto& warning : warnings) {
        log::info("configuration: {}", warning);
    }
    palette_ = theme::make_palette(config_.theme);
}

void Server::watch_config() {
    const auto path = options_.config_path ? std::filesystem::path(*options_.config_path)
                                           : config::config_file();
    auto watcher = FileWatcher::create(path);
    if (!watcher) {
        log::error("configuration will not reload automatically: {}", watcher.error());
        return;
    }
    config_watcher_ = std::move(*watcher);
    config_source_ = wl_event_loop_add_fd(
        wl_display_get_event_loop(display), config_watcher_->fd(), WL_EVENT_READABLE,
        [](int /*fd*/, uint32_t /*mask*/, void* data) {
            auto* self = static_cast<Server*>(data);
            if (self->config_watcher_->changed()) {
                self->reload_config();
            }
            return 0;
        },
        this);
}

void Server::reload_config() {
    std::vector<std::string> warnings;
    auto loaded = config::load(config_watcher_->file(), &warnings);
    if (!loaded) {
        log::error("{}; keeping the previous configuration", loaded.error());
        return;
    }
    for (const auto& warning : warnings) {
        log::info("configuration: {}", warning);
    }
    if (*loaded == config_) {
        return;
    }
    const config::Config previous = std::exchange(config_, std::move(*loaded));
    palette_ = theme::make_palette(config_.theme);
    apply_config(previous);
    log::info("configuration reloaded");
}

void Server::apply_config(const config::Config& previous) {
    if (config_.keyboard != previous.keyboard) {
        for (const auto& keyboard : keyboards_) {
            keyboard->apply_config(config_.keyboard);
        }
    }
    if (config_.mouse != previous.mouse || config_.touchpad != previous.touchpad) {
        for (const auto& pointer : pointers_) {
            pointer->apply_config(config_.mouse, config_.touchpad);
        }
        log::info("mouse and touchpad settings applied to {} libinput device(s)",
                  std::ranges::count_if(pointers_, [](const auto& p) { return p->configurable(); }));
    }
    if (config_.outputs != previous.outputs) {
        // Windows move with their display when it moves in the layout, and
        // are fitted into it once every display is in place (in between,
        // displays may overlap and a window's display is ambiguous).
        struct Placement {
            View* view;
            wlr_output* output;
            wlr_box before;
        };
        std::vector<Placement> placements;
        for (const auto& view : views_) {
            wlr_output* output = view->mapped() ? view->output() : nullptr;
            if (output != nullptr) {
                wlr_box box{};
                wlr_output_layout_get_box(output_layout, output, &box);
                placements.push_back({view.get(), output, box});
            }
        }
        arranging_outputs_ = true;
        for (const auto& output : outputs_) {
            output->apply_config(config_.output(output->wlr()->name));
        }
        arranging_outputs_ = false;
        for (const auto& [view, output, before] : placements) {
            wlr_box after{};
            wlr_output_layout_get_box(output_layout, output, &after);
            if (!wlr_box_empty(&before) && !wlr_box_empty(&after) && (after.x != before.x || after.y != before.y)) {
                view->move_to(view->content().x + after.x - before.x, view->content().y + after.y - before.y);
            }
            view->refit();
        }
    }
    if (config_.desktop != previous.desktop) {
        for (const auto& output : outputs_) {
            output->update_background();
        }
    }
    if (config_.theme != previous.theme || config_.windows != previous.windows) {
        for (const auto& view : views_) {
            view->update_decoration();
            view->refit();
        }
    }
    if (config_.taskbar != previous.taskbar) {
        taskbar_.set_pins(config_.taskbar.pinned);
    }
    if (config_.shell != previous.shell) {
        if (shell_pid_ > 0) {
            kill(shell_pid_, SIGTERM); // the reaper starts the new one
        } else {
            start_shell();
        }
        shell_fast_exits_ = 0;
    }
}

// --- child processes -------------------------------------------------------------

pid_t Server::spawn(const std::string& command) {
    return spawn_argv({"/bin/sh", "-c", command});
}

pid_t Server::spawn_argv(const std::vector<std::string>& argv) {
    // Build argv before fork(): no allocation in the child.
    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const auto& arg : argv) {
        args.push_back(const_cast<char*>(arg.c_str())); // NOLINT: execvp's signature
    }
    args.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        log::error("fork failed: {}", std::strerror(errno)); // NOLINT(concurrency-mt-unsafe)
        return -1;
    }
    if (pid == 0) {
        // libwayland blocks the signals it handles via signalfd; the mask
        // survives exec, so restore it before starting the client.
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        setsid();
        execvp(args.front(), args.data());
        _exit(127);
    }
    log::debug("spawned '{}' (pid {})", argv.back(), pid);
    return pid;
}

std::string Server::locate(const std::string& program, const std::string& build_dir) {
    if (program.find('/') != std::string::npos) {
        return program;
    }
    std::error_code ec;
    const auto exe_dir = std::filesystem::read_symlink("/proc/self/exe", ec).parent_path();
    if (ec) {
        return program;
    }
    // Installed side by side, or in the build tree (build/src/compositor/tetra).
    for (const auto& candidate : {exe_dir / program, exe_dir.parent_path() / build_dir / program}) {
        if (access(candidate.c_str(), X_OK) == 0) {
            return candidate.string();
        }
    }
    return program;
}

void Server::open_app(std::string_view app_id, const std::string& build_dir) {
    for (View* view : focus_order_) {
        if (view->app_id() == app_id) {
            focus_view(view);
            return;
        }
    }
    spawn_argv({locate(std::string(app_id), build_dir)});
}

void Server::start_shell() {
    if (config_.shell.empty() || terminating_ || shell_pid_ > 0) {
        return;
    }
    const std::string program = locate(config_.shell, "shell");
    shell_pid_ = spawn_argv({program});
    shell_started_ = std::chrono::steady_clock::now();
    log::info("started shell {} (pid {})", program, shell_pid_);
}

void Server::reap_children() {
    int status = 0;
    pid_t pid = 0;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (pid != shell_pid_) {
            continue;
        }
        shell_pid_ = -1;
        if (terminating_) {
            continue;
        }
        if (WIFSIGNALED(status)) {
            log::error("shell killed by signal {}", WTERMSIG(status));
        } else {
            log::error("shell exited with status {}", WEXITSTATUS(status));
        }
        const bool fast = std::chrono::steady_clock::now() - shell_started_ < kShellFastExit;
        shell_fast_exits_ = fast ? shell_fast_exits_ + 1 : 0;
        if (shell_fast_exits_ >= kShellMaxFastExits) {
            log::error("shell keeps exiting; not restarting it");
            continue;
        }
        // Back off a little more after each quick exit.
        wl_event_source_timer_update(shell_timer_, 1 + (1000 * shell_fast_exits_));
    }
}

// --- new objects ---------------------------------------------------------------------

void Server::handle_new_output(wlr_output* wlr_output) {
    outputs_.push_back(std::make_unique<Output>(*this, wlr_output));
    schedule_output_manager_update(); // also when it starts disabled
}

void Server::schedule_output_manager_update() {
    if (output_manager_idle_ != nullptr || output_manager_ == nullptr || terminating_) {
        return;
    }
    output_manager_idle_ = wl_event_loop_add_idle(
        wl_display_get_event_loop(display),
        [](void* data) { static_cast<Server*>(data)->update_output_manager(); }, this);
}

void Server::update_output_manager() {
    output_manager_idle_ = nullptr;
    wlr_output_configuration_v1* state = wlr_output_configuration_v1_create();
    for (const auto& output : outputs_) {
        wlr_output_configuration_head_v1* head = wlr_output_configuration_head_v1_create(state, output->wlr());
        if (head == nullptr) {
            continue;
        }
        // In use means in the layout: a blanked screen (idle) is still on.
        wlr_output_layout_output* placed = wlr_output_layout_get(output_layout, output->wlr());
        head->state.enabled = placed != nullptr;
        if (placed != nullptr) {
            head->state.x = placed->x;
            head->state.y = placed->y;
        }
    }
    wlr_output_manager_v1_set_configuration(output_manager_, state); // takes ownership
}

void Server::handle_new_layer_surface(wlr_layer_surface_v1* layer_surface) {
    if (layer_surface->output == nullptr) {
        // Client let us choose: the output under the cursor.
        wlr_output* output = wlr_output_layout_output_at(output_layout, cursor->x, cursor->y);
        if (output == nullptr && !outputs_.empty()) {
            output = outputs_.front()->wlr();
        }
        layer_surface->output = output;
    }
    Output* output = find_output(layer_surface->output);
    if (output == nullptr) {
        wlr_layer_surface_v1_destroy(layer_surface);
        return;
    }
    output->add_layer_surface(layer_surface);
}

void Server::handle_new_popup(wlr_xdg_popup* popup) {
    if (popup->parent == nullptr) {
        return; // layer-shell popups get their parent later; see LayerSurface
    }
    if (wlr_xdg_surface* parent = wlr_xdg_surface_try_from_wlr_surface(popup->parent)) {
        if (parent->data != nullptr) {
            Popup::create(popup, *static_cast<PopupParent*>(parent->data));
            return;
        }
    }
    if (wlr_layer_surface_v1* parent = wlr_layer_surface_v1_try_from_wlr_surface(popup->parent)) {
        if (parent->data != nullptr) {
            Popup::create(popup, *static_cast<PopupParent*>(parent->data));
            return;
        }
    }
    log::error("popup without a known parent; it will not be shown");
}

} // namespace tetra
