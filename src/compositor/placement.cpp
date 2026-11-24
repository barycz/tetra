// Window memory: windows open where they were last, per display
// (core/window_memory.hpp holds the data; this file connects it to views,
// outputs and the state file).

#include "log.hpp"
#include "output.hpp"
#include "server.hpp"
#include "view.hpp"

#include "config_edit.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

namespace tetra {

namespace {

constexpr int kSaveDelayMs = 1000; // windows move in bursts; write once they settle

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

// --- Server ------------------------------------------------------------------------------

std::vector<std::string> Server::display_keys() const {
    std::vector<std::string> keys;
    for (const auto& output : outputs_) {
        if (wlr_output_layout_get(output_layout, output->wlr()) != nullptr) {
            keys.push_back(display_key(output->wlr()));
        }
    }
    return keys;
}

wlr_output* Server::output_for_key(std::string_view key) const {
    for (const auto& output : outputs_) {
        if (wlr_output_layout_get(output_layout, output->wlr()) != nullptr && display_key(output->wlr()) == key) {
            return output->wlr();
        }
    }
    return nullptr;
}

wlr_output* Server::output_at_cursor() const {
    if (wlr_output* output = wlr_output_layout_output_at(output_layout, cursor->x, cursor->y)) {
        return output;
    }
    for (const auto& output : outputs_) {
        if (wlr_output_layout_get(output_layout, output->wlr()) != nullptr) {
            return output->wlr();
        }
    }
    return nullptr;
}

void Server::load_window_memory() {
    window_memory_file_ = windows::state_file();
    window_memory_text_ = read_file(window_memory_file_);
    if (!window_memory_text_.empty()) {
        std::string error;
        window_memory_.replace(windows::Memory::parse(window_memory_text_, &error));
        if (!error.empty()) {
            log::error("{}: {}; starting with no remembered windows", window_memory_file_.string(), error);
        }
    }
    window_memory_timer_ = wl_event_loop_add_timer(
        wl_display_get_event_loop(display),
        [](void* data) {
            static_cast<Server*>(data)->save_window_memory();
            return 0;
        },
        this);

    // Someone else may change or delete the file (Settings forgets
    // positions by deleting it).
    auto watcher = FileWatcher::create(window_memory_file_);
    if (!watcher) {
        log::error("window memory: {}", watcher.error());
        return;
    }
    window_memory_watcher_ = std::move(*watcher);
    window_memory_source_ = wl_event_loop_add_fd(
        wl_display_get_event_loop(display), window_memory_watcher_->fd(), WL_EVENT_READABLE,
        [](int /*fd*/, uint32_t /*mask*/, void* data) {
            auto* self = static_cast<Server*>(data);
            if (self->window_memory_watcher_->changed()) {
                self->reload_window_memory();
            }
            return 0;
        },
        this);
}

void Server::reload_window_memory() {
    std::string text = read_file(window_memory_file_);
    if (text == window_memory_text_) {
        return; // our own save
    }
    window_memory_text_ = std::move(text);
    window_memory_.replace(windows::Memory::parse(window_memory_text_));
    log::info("window memory {}", window_memory_.empty() ? "cleared" : "reloaded");
}

void Server::window_memory_changed() {
    if (window_memory_timer_ != nullptr) {
        wl_event_source_timer_update(window_memory_timer_, kSaveDelayMs);
    }
}

void Server::save_window_memory() {
    if (window_memory_timer_ != nullptr) {
        wl_event_source_timer_update(window_memory_timer_, 0);
    }
    if (window_memory_file_.empty() || (window_memory_.empty() && window_memory_text_.empty())) {
        return; // nothing remembered yet: do not create the file
    }
    std::string text = window_memory_.dump();
    if (text == window_memory_text_) {
        return;
    }
    if (auto written = config::write_file(window_memory_file_, text); !written) {
        log::error("window memory: {}", written.error());
        return;
    }
    window_memory_text_ = std::move(text);
}

void Server::schedule_display_setup_check() {
    if (setup_check_idle_ != nullptr || terminating_) {
        return;
    }
    setup_check_idle_ = wl_event_loop_add_idle(
        wl_display_get_event_loop(display),
        [](void* data) {
            auto* self = static_cast<Server*>(data);
            self->setup_check_idle_ = nullptr;
            self->check_display_setup();
        },
        this);
}

void Server::check_display_setup() {
    const auto keys = display_keys();
    std::string setup = windows::setup_key(keys);
    if (setup == display_setup_ || keys.empty()) {
        return;
    }
    const bool first = display_setup_.empty();
    display_setup_ = std::move(setup);
    if (first) {
        return;
    }
    // Displays came or went: windows go where they were with these displays.
    for (const auto& view : views_) {
        if (view->mapped()) {
            view->restore_place();
        }
    }
}

// --- View --------------------------------------------------------------------------------

std::optional<std::pair<int, int>> View::prepare_placement() {
    if (memory_prepared_) {
        return std::nullopt;
    }
    memory_prepared_ = true;
    if (!server_.config().windows.remember_positions || !is_main_window() || app_id().empty()) {
        return std::nullopt;
    }
    memory_app_ = std::string(app_id());
    auto& memory = server_.window_memory();
    memory_slot_ = memory.claim(memory_app_);
    if (!memory_slot_) {
        return std::nullopt;
    }
    const auto keys = server_.display_keys();
    const auto remembered = memory.display(memory_app_, *memory_slot_, keys);
    wlr_output* output = remembered ? server_.output_for_key(*remembered) : server_.output_at_cursor();
    if (output == nullptr) {
        return std::nullopt;
    }
    memory_display_ = display_key(output);
    auto place = memory.place(memory_app_, *memory_slot_, *memory_display_);
    if (!place) {
        place = memory.last_place(memory_app_, *memory_slot_); // its size, on a new display
    }
    if (!place) {
        return std::nullopt;
    }
    int width = place->width;
    int height = place->height;
    if (const Output* out = server_.find_output(output)) {
        width = std::min(width, out->usable_area().width);
        height = std::min(height, out->usable_area().height);
    }
    return std::pair{width, height};
}

bool View::apply_remembered(const std::string& display, bool first_map) {
    if (!memory_slot_) {
        return false;
    }
    wlr_output* output = server_.output_for_key(display);
    const Output* out = server_.find_output(output);
    if (out == nullptr) {
        return false;
    }
    const auto& memory = server_.window_memory();
    auto place = memory.place(memory_app_, *memory_slot_, display);
    const bool exact = place.has_value();
    if (!place && first_map) {
        place = memory.last_place(memory_app_, *memory_slot_);
    }
    if (!place) {
        return false;
    }
    const wlr_box area = out->usable_area();
    wlr_box box{};
    wlr_output_layout_get_box(server_.output_layout, output, &box);
    wlr_box want{.x = box.x + place->x, .y = box.y + place->y, .width = place->width, .height = place->height};
    if (!exact) { // only the size is known: centre it, as for a new window
        const wlr_box outer = frame();
        const int extra_width = outer.width - content_.width;
        const int extra_height = outer.height - content_.height;
        want.x = area.x + std::max(0, (area.width - want.width - extra_width) / 2) + (content_.x - outer.x);
        want.y = area.y + std::max(0, (area.height - want.height - extra_height) / 2) + (content_.y - outer.y);
    }
    want = fit(want, area);

    if (state_ == State::Fullscreen) {
        return true; // stays fullscreen where it is
    }
    if (place->maximized && !first_map) {
        maximize_on(*out, want);
        return true;
    }
    if (state_ == State::Maximized) {
        state_ = State::Normal;
        send_maximized(false);
        update_decoration();
    }
    move_to(want.x, want.y);
    if (want.width != content_.width || want.height != content_.height) {
        request_size(want.width, want.height);
    }
    if (place->maximized) {
        remembered_maximized_ = want; // maximized once mapped
    }
    return true;
}

void View::restore_place() {
    if (!memory_slot_) {
        return;
    }
    const auto keys = server_.display_keys();
    const auto display = server_.window_memory().display(memory_app_, *memory_slot_, keys);
    if (display) {
        apply_remembered(*display, /*first_map=*/false);
    }
}

void View::remember_place() {
    if (!memory_slot_ || !mapped_ || state_ == State::Fullscreen || !server_.config().windows.remember_positions) {
        return;
    }
    wlr_output* out = output();
    if (out == nullptr) {
        return;
    }
    wlr_box box{};
    wlr_output_layout_get_box(server_.output_layout, out, &box);
    const wlr_box& normal = state_ == State::Maximized ? saved_ : content_;
    if (normal.width <= 0 || normal.height <= 0) {
        return;
    }
    const windows::Place place{.x = normal.x - box.x,
                               .y = normal.y - box.y,
                               .width = normal.width,
                               .height = normal.height,
                               .maximized = state_ == State::Maximized};
    server_.window_memory().remember(memory_app_, *memory_slot_, server_.display_keys(), display_key(out), place,
                                     now_seconds());
    server_.window_memory_changed();
}

void View::release_slot() {
    if (memory_slot_) {
        server_.window_memory().release(memory_app_, *memory_slot_);
        memory_slot_.reset();
    }
}

} // namespace tetra
