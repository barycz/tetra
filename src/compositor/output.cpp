#include "output.hpp"

#include "animation.hpp"

#include "layer_surface.hpp"
#include "log.hpp"
#include "server.hpp"
#include "wallpaper.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <format>
#include <string_view>
#include <utility>

namespace tetra {

std::string display_key(const wlr_output* output) {
    if (output->serial == nullptr || *output->serial == '\0') {
        return output->name;
    }
    std::string key;
    for (const char* part : {output->make, output->model, output->serial}) {
        if (part != nullptr && *part != '\0') {
            if (!key.empty()) {
                key += ' ';
            }
            key += part;
        }
    }
    return key;
}

namespace {

constexpr double kSlowFrameMs = 12;

// Output size for backends without modes (headless, nested), e.g. "1920x1080".
std::optional<config::OutputMode> env_output_size() {
    const char* env = std::getenv("TETRA_OUTPUT_SIZE"); // NOLINT(concurrency-mt-unsafe)
    if (env == nullptr) {
        return std::nullopt;
    }
    const std::string_view text{env};
    const auto sep = text.find('x');
    config::OutputMode mode;
    if (sep == std::string_view::npos ||
        std::from_chars(text.data(), text.data() + sep, mode.width).ec != std::errc{} ||
        std::from_chars(text.data() + sep + 1, text.data() + text.size(), mode.height).ec !=
            std::errc{} ||
        mode.width <= 0 || mode.height <= 0) {
        log::error("ignoring malformed TETRA_OUTPUT_SIZE='{}'", text);
        return std::nullopt;
    }
    return mode;
}

} // namespace

Output::Output(Server& server, wlr_output* wlr_output) : server_(server), output_(wlr_output) {
    wlr_output_init_render(output_, server_.allocator, server_.renderer);

    frame_.connect(output_->events.frame, [this] { handle_frame(); });
    frame_timer_ = wl_event_loop_add_timer(
        wl_display_get_event_loop(server_.display),
        [](void* data) {
            auto* self = static_cast<Output*>(data);
            self->frame_timer_armed_ = false;
            wlr_output_schedule_frame(self->output_); // the postponed frame
            return 0;
        },
        this);
    request_state_.connect<wlr_output_event_request_state>(
        output_->events.request_state, [this](auto* event) {
            // Nested backends ask for a new mode when their window is resized.
            wlr_output_commit_state(output_, event->state);
            update_background();
            arrange_layers();
        });
    destroy_.connect(output_->events.destroy, [this] { server_.destroy_output(*this); });

    background_ = wlr_scene_rect_create(server_.layers.background, 0, 0,
                                        server_.config().desktop.background.premultiplied().data());
    wlr_scene_node_lower_to_bottom(&background_->node);
    apply_config(server_.config().output(output_->name));
}

Output::~Output() {
    wl_event_source_remove(frame_timer_);
    // Closing a layer surface removes it from layer_surfaces_ via its destroy handler.
    while (!layer_surfaces_.empty()) {
        wlr_layer_surface_v1_destroy(layer_surfaces_.back()->wlr());
    }
    if (wallpaper_ != nullptr) {
        wlr_scene_node_destroy(&wallpaper_->node);
    }
    wlr_scene_node_destroy(&background_->node);
}

wlr_box Output::box() const {
    wlr_box box{};
    wlr_output_layout_get_box(server_.output_layout, output_, &box);
    return box;
}

bool Output::set_mode(wlr_output_state& state, const config::OutputMode& wanted) {
    if (wl_list_empty(&output_->modes)) {
        // Headless/nested: any size goes.
        wlr_output_state_set_custom_mode(&state, wanted.width, wanted.height,
                                         static_cast<int32_t>(std::lround(wanted.refresh * 1000)));
        return true;
    }
    wlr_output_mode* best = nullptr;
    for (wl_list* link = output_->modes.next; link != &output_->modes; link = link->next) {
        auto* mode = container_of<wlr_output_mode>(link, offsetof(wlr_output_mode, link));
        if (mode->width != wanted.width || mode->height != wanted.height) {
            continue;
        }
        if (wanted.refresh > 0) {
            const double diff = std::abs((mode->refresh / 1000.0) - wanted.refresh);
            if (best == nullptr || diff < std::abs((best->refresh / 1000.0) - wanted.refresh)) {
                best = mode;
            }
        } else if (best == nullptr || mode->refresh > best->refresh) {
            best = mode;
        }
    }
    if (best == nullptr) {
        log::error("output {}: no mode {}x{}; keeping the default", output_->name, wanted.width,
                   wanted.height);
        return false;
    }
    wlr_output_state_set_mode(&state, best);
    return true;
}

void Output::set_power(bool on) {
    if (on == powered_) {
        return;
    }
    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, on);
    if (on) {
        if (saved_mode_ != nullptr) {
            wlr_output_state_set_mode(&state, saved_mode_);
        } else if (saved_custom_mode_.width > 0) {
            set_mode(state, saved_custom_mode_);
        }
    } else {
        saved_mode_ = output_->current_mode;
        saved_custom_mode_ = {.width = output_->width,
                              .height = output_->height,
                              .refresh = output_->refresh / 1000.0};
    }
    if (wlr_output_commit_state(output_, &state)) {
        powered_ = on;
        log::info("output {}: screen {}", output_->name, on ? "on" : "off");
    } else {
        log::error("output {}: cannot turn the screen {}", output_->name, on ? "on" : "off");
    }
    wlr_output_state_finish(&state);
}

void Output::apply_config(const config::Output* config) {
    const bool enabled = config == nullptr || config->enabled;
    powered_ = true; // a configuration change wakes a blanked screen

    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, enabled);
    if (enabled) {
        bool mode_set = config != nullptr && config->mode && set_mode(state, *config->mode);
        if (!mode_set && output_->current_mode == nullptr) {
            if (wlr_output_mode* preferred = wlr_output_preferred_mode(output_)) {
                wlr_output_state_set_mode(&state, preferred);
            } else if (auto size = env_output_size()) {
                set_mode(state, *size);
            }
        }
        wlr_output_state_set_scale(&state, config != nullptr ? static_cast<float>(config->scale) : 1.0F);
    }
    if (!wlr_output_commit_state(output_, &state)) {
        log::error("output {}: failed to apply configuration", output_->name);
    }
    wlr_output_state_finish(&state);

    if (enabled) {
        if (config != nullptr && config->position) {
            wlr_output_layout_add(server_.output_layout, output_, config->position->first,
                                  config->position->second);
        } else if (wlr_output_layout_get(server_.output_layout, output_) == nullptr) {
            wlr_output_layout_add_auto(server_.output_layout, output_);
        }
        ensure_scene_output();
    } else {
        wlr_output_layout_remove(server_.output_layout, output_);
        // Its scene output no longer follows the layout: drop it, so turning
        // the display on again creates one at its new position (instead of
        // drawing the old area, mirroring another display).
        if (wlr_scene_output* scene_output = wlr_scene_get_scene_output(server_.scene, output_)) {
            wlr_scene_output_destroy(scene_output);
        }
    }
    log::info("output {}: {} {}x{} scale {:.2f}", output_->name, enabled ? "on" : "off",
              output_->width, output_->height, output_->scale);

    update_background();
    arrange_layers();
}

void Output::ensure_scene_output() {
    if (wlr_scene_get_scene_output(server_.scene, output_) != nullptr) {
        return;
    }
    wlr_scene_output* scene_output = wlr_scene_output_create(server_.scene, output_);
    wlr_output_layout_output* layout_output = wlr_output_layout_get(server_.output_layout, output_);
    wlr_scene_output_layout_add_output(server_.scene_layout, layout_output, scene_output);
}

void Output::update_background() {
    const wlr_box area = box();
    const bool shown = !wlr_box_empty(&area);
    wlr_scene_node_set_enabled(&background_->node, shown);
    wlr_scene_node_set_position(&background_->node, area.x, area.y);
    wlr_scene_rect_set_size(background_, area.width, area.height);
    wlr_scene_rect_set_color(background_,
                             server_.config().desktop.background.premultiplied().data());

    // The logo or picture over it, drawn at the display's own resolution
    // (again only when that or what it shows changed).
    Wallpaper& wallpaper = server_.wallpaper();
    const float scale = output_->scale;
    if (shown) {
        int width = 0;
        int height = 0;
        wlr_output_transformed_resolution(output_, &width, &height);
        wallpaper.ensure_covers(width, height);
    }
    const WallpaperKey key{.width = shown ? area.width : 0,
                           .height = shown ? area.height : 0,
                           .scale = scale,
                           .generation = wallpaper.generation()};
    if (key != wallpaper_key_) {
        wallpaper_key_ = key;
        wlr_buffer* buffer = shown ? wallpaper.render(area.width, area.height, scale) : nullptr;
        if (buffer != nullptr && wallpaper_ == nullptr) {
            wallpaper_ = wlr_scene_buffer_create(server_.layers.background, nullptr);
            wlr_scene_node_place_above(&wallpaper_->node, &background_->node);
        }
        if (wallpaper_ != nullptr) {
            wlr_scene_buffer_set_buffer(wallpaper_, buffer);
            wlr_scene_buffer_set_dest_size(wallpaper_, area.width, area.height);
        }
        if (buffer != nullptr) {
            wlr_buffer_drop(buffer); // the scene buffer holds it
        }
    }
    if (wallpaper_ != nullptr) {
        wlr_scene_node_set_position(&wallpaper_->node, area.x, area.y);
        wlr_scene_node_set_enabled(&wallpaper_->node, shown && wallpaper_->buffer != nullptr);
    }
}

void Output::arrange_layers() {
    const wlr_box full = box();
    wlr_box usable = full;
    if (!wlr_box_empty(&full)) {
        // Surfaces with an exclusive zone claim their edge first, top layer
        // first; then everything else is placed in what remains.
        constexpr std::array<zwlr_layer_shell_v1_layer, 4> order = {
            ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, ZWLR_LAYER_SHELL_V1_LAYER_TOP,
            ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND};
        for (const bool exclusive : {true, false}) {
            for (const auto layer : order) {
                for (const auto& surface : layer_surfaces_) {
                    wlr_layer_surface_v1* ls = surface->wlr();
                    if (!ls->initialized || ls->current.layer != layer ||
                        (ls->current.exclusive_zone > 0) != exclusive) {
                        continue;
                    }
                    wlr_scene_layer_surface_v1_configure(surface->scene(), &full, &usable);
                }
            }
        }
    }
    if (!wlr_box_equal(&usable, &usable_area_)) {
        usable_area_ = usable;
        server_.handle_usable_area_changed(*this);
    }
}

void Output::add_layer_surface(wlr_layer_surface_v1* layer_surface) {
    layer_surfaces_.push_back(std::make_unique<LayerSurface>(server_, *this, layer_surface));
}

void Output::remove_layer_surface(LayerSurface& surface) {
    server_.handle_layer_destroyed(surface);
    std::erase_if(layer_surfaces_, [&](const auto& ptr) { return ptr.get() == &surface; });
    arrange_layers();
}

void Output::handle_frame() {
    // Some drivers (vmwgfx in VirtualBox) report page flips as done at once
    // instead of at vblank, so wlroots asks for the next frame immediately.
    // Uncapped, that renders hundreds of frames per second, and every frame
    // invites clients to draw again (frame callbacks): Firefox repainted its
    // page that often while hovered. Never go faster than the refresh rate.
    // With real vblank the interval is a full period and this never triggers.
    const auto now = std::chrono::steady_clock::now();
    const int refresh_mhz = output_->refresh > 0 ? output_->refresh : 60000;
    const auto period = std::chrono::nanoseconds(1'000'000'000'000LL / refresh_mhz);
    const auto elapsed = now - last_frame_;
    if (elapsed < period * 3 / 4) {
        ++stats_.postponed;
        if (!frame_timer_armed_) {
            const auto wait = std::chrono::ceil<std::chrono::milliseconds>(period - elapsed);
            wl_event_source_timer_update(frame_timer_, static_cast<int>(std::max<int64_t>(1, wait.count())));
            frame_timer_armed_ = true;
        }
        return;
    }
    last_frame_ = now;
    render_frame();
}

void Output::render_frame() {
    wlr_scene_output* scene_output = wlr_scene_get_scene_output(server_.scene, output_);
    if (scene_output == nullptr) {
        return;
    }
    const auto start = std::chrono::steady_clock::now();
    const bool animating = server_.animator().tick(start);
    const bool drawn = wlr_scene_output_needs_frame(scene_output);
    wlr_scene_output_commit(scene_output, nullptr);
    const auto end = std::chrono::steady_clock::now();
    if (animating) {
        wlr_output_schedule_frame(output_); // the next step
    }

    // Diagnostics: the event loop (input included) waits while a frame
    // renders, so slow frames show up as input lag. Rate-limited.
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();
    ++stats_.frames;
    stats_.drawn += drawn ? 1 : 0;
    stats_.total_ms += ms;
    stats_.max_ms = std::max(stats_.max_ms, ms);
    if (ms > kSlowFrameMs && end - last_slow_frame_log_ > std::chrono::seconds(2)) {
        last_slow_frame_log_ = end;
        log::info("output {}: slow frame, {:.1f} ms to render and commit", output_->name, ms);
    }

    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
    server_.handle_output_frame(output_);
}

std::string Output::take_stats() {
    const FrameStats s = std::exchange(stats_, {});
    const double avg = s.frames > 0 ? s.total_ms / s.frames : 0;
    return std::format(
        "output {}: {} frames ({} drawn, {} postponed), avg {:.1f} ms, max {:.1f} ms, {} cursor",
        output_->name, s.frames, s.drawn, s.postponed, avg, s.max_ms,
        output_->hardware_cursor != nullptr ? "hardware" : "software");
}

} // namespace tetra
