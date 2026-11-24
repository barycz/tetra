#include "displays.hpp"

#include <algorithm>
#include <cmath>

namespace tetra::settings {

int Display::logical_width() const {
    return current != nullptr ? static_cast<int>(std::lround(current->width / scale)) : 0;
}

int Display::logical_height() const {
    return current != nullptr ? static_cast<int>(std::lround(current->height / scale)) : 0;
}

bool Display::any_size() const {
    return modes.size() <= 1 && std::ranges::none_of(modes, [](const auto& m) { return m->preferred; });
}

std::string Display::title() const {
    std::string text = make;
    if (!model.empty()) {
        text += (text.empty() ? "" : " ") + model;
    }
    if (text.empty() || text == "Unknown Unknown") {
        text = description.empty() ? name : description;
    }
    return text;
}

// libwayland calls these with the object's user data.
struct DisplayListeners {
    static Displays* self(void* data) { return static_cast<Displays*>(data); }

    // --- manager -------------------------------------------------------------------
    static void head(void* data, zwlr_output_manager_v1* /*manager*/, zwlr_output_head_v1* proxy) {
        auto display = std::make_unique<Display>();
        display->proxy = proxy;
        display->list = self(data);
        zwlr_output_head_v1_add_listener(proxy, &head_listener, display.get());
        self(data)->displays_.push_back(std::move(display));
    }
    static void done(void* data, zwlr_output_manager_v1* /*manager*/, uint32_t /*serial*/) {
        // Numbered in a stable order: by connector.
        std::ranges::sort(self(data)->displays_, {}, [](const auto& d) -> const std::string& { return d->name; });
        if (self(data)->on_changed) {
            self(data)->on_changed();
        }
    }
    static void finished(void* data, zwlr_output_manager_v1* manager) {
        zwlr_output_manager_v1_destroy(manager);
        self(data)->manager_ = nullptr;
    }
    static constexpr zwlr_output_manager_v1_listener manager_listener{
        .head = head, .done = done, .finished = finished};

    // --- head ---------------------------------------------------------------------
    static Display* display(void* data) { return static_cast<Display*>(data); }
    static void name(void* data, zwlr_output_head_v1* /*head*/, const char* value) { display(data)->name = value; }
    static void description(void* data, zwlr_output_head_v1* /*head*/, const char* value) {
        display(data)->description = value;
    }
    static void physical_size(void* data, zwlr_output_head_v1* /*head*/, int32_t width, int32_t height) {
        display(data)->physical_width = width;
        display(data)->physical_height = height;
    }
    static void mode(void* data, zwlr_output_head_v1* /*head*/, zwlr_output_mode_v1* proxy) {
        auto mode = std::make_unique<DisplayMode>();
        mode->proxy = proxy;
        mode->owner = display(data);
        zwlr_output_mode_v1_add_listener(proxy, &mode_listener, mode.get());
        display(data)->modes.push_back(std::move(mode));
    }
    static void enabled(void* data, zwlr_output_head_v1* /*head*/, int32_t value) {
        display(data)->enabled = value != 0;
    }
    static void current_mode(void* data, zwlr_output_head_v1* /*head*/, zwlr_output_mode_v1* proxy) {
        Display* d = display(data);
        auto it = std::ranges::find_if(d->modes, [&](const auto& m) { return m->proxy == proxy; });
        d->current = it != d->modes.end() ? it->get() : nullptr;
    }
    static void position(void* data, zwlr_output_head_v1* /*head*/, int32_t x, int32_t y) {
        display(data)->x = x;
        display(data)->y = y;
    }
    static void transform(void* /*data*/, zwlr_output_head_v1* /*head*/, int32_t /*value*/) {}
    static void scale(void* data, zwlr_output_head_v1* /*head*/, wl_fixed_t value) {
        display(data)->scale = wl_fixed_to_double(value);
    }
    static void head_finished(void* data, zwlr_output_head_v1* /*head*/);
    static void make(void* data, zwlr_output_head_v1* /*head*/, const char* value) { display(data)->make = value; }
    static void model(void* data, zwlr_output_head_v1* /*head*/, const char* value) {
        display(data)->model = value;
    }
    static void serial_number(void* /*data*/, zwlr_output_head_v1* /*head*/, const char* /*value*/) {}
    static void adaptive_sync(void* /*data*/, zwlr_output_head_v1* /*head*/, uint32_t /*state*/) {}
    static constexpr zwlr_output_head_v1_listener head_listener{
        .name = name,
        .description = description,
        .physical_size = physical_size,
        .mode = mode,
        .enabled = enabled,
        .current_mode = current_mode,
        .position = position,
        .transform = transform,
        .scale = scale,
        .finished = head_finished,
        .make = make,
        .model = model,
        .serial_number = serial_number,
        .adaptive_sync = adaptive_sync,
    };

    // --- mode -----------------------------------------------------------------------
    static DisplayMode* dmode(void* data) { return static_cast<DisplayMode*>(data); }
    static void size(void* data, zwlr_output_mode_v1* /*mode*/, int32_t width, int32_t height) {
        dmode(data)->width = width;
        dmode(data)->height = height;
    }
    static void refresh(void* data, zwlr_output_mode_v1* /*mode*/, int32_t value) { dmode(data)->refresh = value; }
    static void preferred(void* data, zwlr_output_mode_v1* /*mode*/) { dmode(data)->preferred = true; }
    static void mode_finished(void* data, zwlr_output_mode_v1* /*mode*/);
    static constexpr zwlr_output_mode_v1_listener mode_listener{
        .size = size, .refresh = refresh, .preferred = preferred, .finished = mode_finished};

};

void DisplayListeners::head_finished(void* data, zwlr_output_head_v1* /*head*/) {
    Display* d = display(data);
    d->list->remove_display(d);
}

void DisplayListeners::mode_finished(void* data, zwlr_output_mode_v1* /*mode*/) {
    DisplayMode* m = dmode(data);
    m->owner->list->remove_mode(m);
}

Displays::Displays(ui::App& app) {
    manager_ = static_cast<zwlr_output_manager_v1*>(app.bind_global(&zwlr_output_manager_v1_interface, 4));
    if (manager_ != nullptr) {
        zwlr_output_manager_v1_add_listener(manager_, &DisplayListeners::manager_listener, this);
    }
}

Displays::~Displays() {
    while (!displays_.empty()) {
        remove_display(displays_.back().get());
    }
    if (manager_ != nullptr) {
        zwlr_output_manager_v1_destroy(manager_);
    }
}

void Displays::remove_mode(DisplayMode* mode) {
    Display* owner = mode->owner;
    if (owner->current == mode) {
        owner->current = nullptr;
    }
    if (zwlr_output_mode_v1_get_version(mode->proxy) >= ZWLR_OUTPUT_MODE_V1_RELEASE_SINCE_VERSION) {
        zwlr_output_mode_v1_release(mode->proxy);
    } else {
        zwlr_output_mode_v1_destroy(mode->proxy);
    }
    std::erase_if(owner->modes, [&](const auto& m) { return m.get() == mode; });
}

void Displays::remove_display(Display* display) {
    while (!display->modes.empty()) {
        remove_mode(display->modes.back().get());
    }
    if (zwlr_output_head_v1_get_version(display->proxy) >= ZWLR_OUTPUT_HEAD_V1_RELEASE_SINCE_VERSION) {
        zwlr_output_head_v1_release(display->proxy);
    } else {
        zwlr_output_head_v1_destroy(display->proxy);
    }
    std::erase_if(displays_, [&](const auto& d) { return d.get() == display; });
}

} // namespace tetra::settings
