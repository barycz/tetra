// Input: devices, pointer handling and decorations, interactive move/resize,
// compositor key bindings.

#include "keyboard.hpp"
#include "layer_surface.hpp"
#include "log.hpp"
#include "server.hpp"
#include "session_lock.hpp"
#include "shell_protocol.hpp"

#include "tetra-shell-unstable-v1-protocol.h"
#include "view.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

namespace tetra {

namespace {

constexpr uint32_t kCursorSize = 24;
constexpr uint32_t kDoubleClickMs = 400;
constexpr double kDragThreshold = 4;
constexpr int kMinWidth = 120;
constexpr int kMinHeight = 60;

} // namespace

void Server::init_input() {
    cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(cursor, output_layout);
    cursor_manager = wlr_xcursor_manager_create(nullptr, kCursorSize);

    cursor_motion_.connect<wlr_pointer_motion_event>(cursor->events.motion, [this](auto* event) {
        notify_activity();
        wlr_cursor_move(cursor, &event->pointer->base, event->delta_x, event->delta_y);
        handle_cursor_motion(event->time_msec);
    });
    cursor_motion_absolute_.connect<wlr_pointer_motion_absolute_event>(
        cursor->events.motion_absolute, [this](auto* event) {
            notify_activity();
            wlr_cursor_warp_absolute(cursor, &event->pointer->base, event->x, event->y);
            handle_cursor_motion(event->time_msec);
        });
    cursor_button_.connect<wlr_pointer_button_event>(cursor->events.button, [this](auto* event) {
        notify_activity();
        flush_pointer_motion(); // clients must see where the click happens
        handle_cursor_button(*event);
        pointer_frame_pending_ = true;
    });
    cursor_axis_.connect<wlr_pointer_axis_event>(cursor->events.axis, [this](auto* event) {
        notify_activity();
        flush_pointer_motion();
        wlr_seat_pointer_notify_axis(seat, event->time_msec, event->orientation, event->delta,
                                     event->delta_discrete, event->source,
                                     event->relative_direction);
        pointer_frame_pending_ = true;
    });
    cursor_frame_.connect(cursor->events.frame, [this] {
        // Motion-only frames are sent by flush_pointer_motion() instead.
        if (pointer_frame_pending_) {
            pointer_frame_pending_ = false;
            wlr_seat_pointer_notify_frame(seat);
        }
    });
    motion_timer_ = wl_event_loop_add_timer(
        wl_display_get_event_loop(display),
        [](void* data) {
            static_cast<Server*>(data)->flush_pointer_motion();
            return 0;
        },
        this);

    new_input_.connect<wlr_input_device>(
        backend->events.new_input, [this](wlr_input_device* device) { handle_new_input(device); });

    // Virtual input devices (wlrctl, on-screen keyboards, test automation).
    auto* virtual_pointers = wlr_virtual_pointer_manager_v1_create(display);
    new_virtual_pointer_.connect<wlr_virtual_pointer_v1_new_pointer_event>(
        virtual_pointers->events.new_virtual_pointer,
        [this](auto* event) { handle_new_input(&event->new_pointer->pointer.base); });
    auto* virtual_keyboards = wlr_virtual_keyboard_manager_v1_create(display);
    new_virtual_keyboard_.connect<wlr_virtual_keyboard_v1>(
        virtual_keyboards->events.new_virtual_keyboard,
        [this](auto* keyboard) { handle_new_input(&keyboard->keyboard.base); });

    seat = wlr_seat_create(display, "seat0");
    update_seat_capabilities();
    request_cursor_.connect<wlr_seat_pointer_request_set_cursor_event>(
        seat->events.request_set_cursor, [this](auto* event) {
            // Any client may ask; only the one with pointer focus gets its way.
            if (event->seat_client == seat->pointer_state.focused_client) {
                cursor_image_ = nullptr;
                wlr_cursor_set_surface(cursor, event->surface, event->hotspot_x, event->hotspot_y);
            }
        });
    auto* cursor_shapes = wlr_cursor_shape_manager_v1_create(display, 1);
    request_cursor_shape_.connect<wlr_cursor_shape_manager_v1_request_set_shape_event>(
        cursor_shapes->events.request_set_shape, [this](auto* event) {
            if (event->seat_client == seat->pointer_state.focused_client) {
                cursor_image_ = nullptr;
                wlr_cursor_set_xcursor(cursor, cursor_manager,
                                       wlr_cursor_shape_v1_name(event->shape));
            }
        });
    pointer_focus_change_.connect<wlr_seat_pointer_focus_change_event>(
        seat->pointer_state.events.focus_change, [this](auto* event) {
            if (event->new_surface == nullptr) {
                set_cursor_image("default");
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
    // Always advertise both: we always draw a cursor, and clients that hold a
    // wl_keyboard from the start get keys from keyboards plugged in later
    // (or virtual keyboards) without racing their own setup.
    wlr_seat_set_capabilities(seat, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
}

// --- pointer -----------------------------------------------------------------------

Server::Hit Server::hit_test(double lx, double ly) {
    Hit hit;
    double nx = 0;
    double ny = 0;
    wlr_scene_node* node = wlr_scene_node_at(&scene->tree.node, lx, ly, &nx, &ny);
    if (node == nullptr) {
        return hit;
    }
    if (node->type == WLR_SCENE_NODE_BUFFER) {
        if (wlr_scene_surface* surface =
                wlr_scene_surface_try_from_buffer(wlr_scene_buffer_from_node(node))) {
            hit.surface = surface->surface;
            hit.sx = nx;
            hit.sy = ny;
        }
    }
    if (SceneDescriptor* descriptor = find_descriptor(node)) {
        switch (descriptor->role) {
        case SceneDescriptor::Role::View:
            hit.view = static_cast<View*>(descriptor->object);
            break;
        case SceneDescriptor::Role::Decoration:
            hit.view = static_cast<View*>(descriptor->object);
            if (const Decoration* decoration = hit.view->decoration()) {
                hit.decoration = decoration->hit_test(lx - hit.view->content().x,
                                                      ly - hit.view->content().y);
            }
            break;
        case SceneDescriptor::Role::LayerSurface:
            hit.layer = static_cast<LayerSurface*>(descriptor->object);
            break;
        }
    }
    return hit;
}

void Server::set_cursor_image(const char* name) {
    if (cursor_image_ != nullptr && std::strcmp(cursor_image_, name) == 0) {
        return;
    }
    cursor_image_ = name;
    wlr_cursor_set_xcursor(cursor, cursor_manager, name);
}

void Server::set_hovered_decoration(View* view, Decoration::Part part) {
    if (hovered_view_ != nullptr && hovered_view_ != view) {
        if (Decoration* old = hovered_view_->decoration()) {
            old->set_hover(Decoration::Part::None);
        }
    }
    hovered_view_ = view;
    if (view != nullptr) {
        if (Decoration* decoration = view->decoration()) {
            decoration->set_hover(part);
        }
    }
}

void Server::handle_cursor_motion(uint32_t time_msec) {
    ++stats_pointer_events_;
    switch (grab_.mode) {
    case GrabMode::PendingMove:
        if (std::hypot(cursor->x - grab_.x, cursor->y - grab_.y) >= kDragThreshold) {
            begin_interactive_move(*grab_.view);
        }
        return;
    case GrabMode::Move:
        process_move();
        return;
    case GrabMode::Resize:
        process_resize();
        return;
    case GrabMode::Button:
        return; // hover stays on the pressed button
    case GrabMode::None:
        break;
    }

    // The cursor image already moved (wlr_cursor_move); clients get at most
    // one motion per display refresh, with the latest position. Pointing
    // devices can report at up to 1000 Hz (VM tablets, gaming mice), and
    // apps like Firefox hit-test and repaint for every event they receive.
    pending_motion_time_ = time_msec;
    if (motion_pending_) {
        return; // a flush is already scheduled
    }
    motion_pending_ = true;
    const auto since = std::chrono::steady_clock::now() - last_motion_flush_;
    const auto interval = motion_interval();
    if (since >= interval) {
        flush_pointer_motion();
        return;
    }
    const auto wait = std::chrono::ceil<std::chrono::milliseconds>(interval - since);
    wl_event_source_timer_update(motion_timer_, static_cast<int>(std::max<int64_t>(1, wait.count())));
}

std::chrono::microseconds Server::motion_interval() const {
    const wlr_output* output = wlr_output_layout_output_at(output_layout, cursor->x, cursor->y);
    const int refresh_mhz = output != nullptr && output->refresh > 0 ? output->refresh : 60000;
    const auto frame = std::chrono::microseconds(1'000'000'000LL / refresh_mhz);
    return std::clamp(frame, std::chrono::microseconds(4000), std::chrono::microseconds(16667));
}

void Server::flush_pointer_motion() {
    if (!motion_pending_) {
        return;
    }
    motion_pending_ = false;
    wl_event_source_timer_update(motion_timer_, 0); // disarm
    last_motion_flush_ = std::chrono::steady_clock::now();

    const Hit hit = hit_test(cursor->x, cursor->y);
    const bool on_decoration = hit.view != nullptr && hit.decoration.part != Decoration::Part::None;
    set_hovered_decoration(on_decoration ? hit.view : nullptr, hit.decoration.part);

    if (hit.surface != nullptr) {
        wlr_seat_pointer_notify_enter(seat, hit.surface, hit.sx, hit.sy);
        wlr_seat_pointer_notify_motion(seat, pending_motion_time_, hit.sx, hit.sy);
        ++stats_motions_sent_;
    } else {
        wlr_seat_pointer_clear_focus(seat);
        set_cursor_image(on_decoration ? Decoration::cursor_for(hit.decoration) : "default");
    }
    wlr_seat_pointer_notify_frame(seat);
}

void Server::schedule_pointer_refocus() {
    if (refocus_idle_ != nullptr || terminating_) {
        return;
    }
    auto refocus = [](void* data) {
        auto* self = static_cast<Server*>(data);
        self->refocus_idle_ = nullptr;
        // Never steal focus in the middle of a drag or a decoration press.
        if (self->grab_.mode != GrabMode::None || self->seat->pointer_state.button_count > 0) {
            return;
        }
        timespec now{};
        clock_gettime(CLOCK_MONOTONIC, &now);
        self->pending_motion_time_ =
            static_cast<uint32_t>((now.tv_sec * 1000) + (now.tv_nsec / 1'000'000));
        self->motion_pending_ = true;
        self->flush_pointer_motion();
    };
    refocus_idle_ = wl_event_loop_add_idle(wl_display_get_event_loop(display), refocus, this);
}

void Server::handle_cursor_button(const wlr_pointer_button_event& event) {
    if (event.state == WL_POINTER_BUTTON_STATE_RELEASED) {
        const bool consumed = consumed_press_;
        consumed_press_ = false;
        if (grab_.mode == GrabMode::Button) {
            const Hit hit = hit_test(cursor->x, cursor->y);
            View* view = grab_.view;
            const Decoration::Part button = grab_.button;
            end_grab();
            if (hit.view == view && hit.decoration.part == button) {
                switch (button) {
                case Decoration::Part::Minimize:
                    view->set_minimized(true);
                    break;
                case Decoration::Part::Maximize:
                    view->toggle_maximized();
                    break;
                case Decoration::Part::Close:
                    view->close();
                    break;
                default:
                    break;
                }
            }
        } else if (grab_.mode != GrabMode::None) {
            end_grab();
        }
        if (!consumed) {
            wlr_seat_pointer_notify_button(seat, event.time_msec, event.button, event.state);
        }
        return;
    }

    for (const auto& keyboard : keyboards_) {
        keyboard->cancel_super_tap(); // Super+click is not a Super tap
    }
    const Hit hit = hit_test(cursor->x, cursor->y);
    if (locked()) {
        // Only lock surfaces are reachable (the cover hides the rest); a
        // click picks the one that gets the keyboard.
        if (hit.surface != nullptr) {
            session_lock_->focus(hit.surface);
        }
        wlr_seat_pointer_notify_button(seat, event.time_msec, event.button, event.state);
        return;
    }
    log::debug("press at {:.0f},{:.0f}: view '{}' decoration part {}", cursor->x, cursor->y,
               hit.view != nullptr ? hit.view->title() : "-",
               static_cast<int>(hit.decoration.part));
    if (hit.layer != nullptr) {
        if (hit.layer->keyboard() != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
            focused_layer_ = hit.layer;
            restore_keyboard_focus();
        }
    } else if (hit.view != nullptr) {
        focus_view(hit.view);
        if (hit.decoration.part != Decoration::Part::None) {
            consumed_press_ = true; // the client never sees this click
            handle_decoration_press(*hit.view, hit.decoration, event.time_msec);
            return;
        }
    }
    wlr_seat_pointer_notify_button(seat, event.time_msec, event.button, event.state);
}

void Server::handle_decoration_press(View& view, const Decoration::Hit& hit, uint32_t time_msec) {
    switch (hit.part) {
    case Decoration::Part::Titlebar:
        if (last_title_click_view_ == &view && time_msec - last_title_click_time_ < kDoubleClickMs) {
            last_title_click_view_ = nullptr;
            view.toggle_maximized();
            return;
        }
        last_title_click_view_ = &view;
        last_title_click_time_ = time_msec;
        // Start moving only once the pointer actually drags, so a double
        // click (or a click on a maximized window) does not move anything.
        grab_ = {.mode = GrabMode::PendingMove, .view = &view, .x = cursor->x, .y = cursor->y};
        return;
    case Decoration::Part::Edge:
        begin_interactive_resize(view, hit.edges);
        return;
    case Decoration::Part::Minimize:
    case Decoration::Part::Maximize:
    case Decoration::Part::Close:
        grab_ = {.mode = GrabMode::Button, .view = &view, .button = hit.part};
        return;
    case Decoration::Part::None:
        return;
    }
}

void Server::end_grab() {
    grab_ = {};
}

void Server::begin_interactive_move(View& view) {
    if (view.state() == View::State::Fullscreen) {
        return;
    }
    if (view.state() == View::State::Maximized) {
        // Like Windows: dragging a maximized window restores it under the
        // pointer, keeping the grab point proportionally in the same place.
        const wlr_box c = view.content();
        const double ratio = c.width > 0 ? (cursor->x - c.x) / c.width : 0.5;
        const double dy = cursor->y - c.y;
        const int width = view.saved_geometry().width;
        view.set_maximized(false);
        view.move_to(static_cast<int>(std::lround(cursor->x - (ratio * width))),
                     static_cast<int>(std::lround(cursor->y - dy)));
    }
    grab_ = {.mode = GrabMode::Move,
             .view = &view,
             .x = cursor->x - view.content().x,
             .y = cursor->y - view.content().y};
}

void Server::begin_interactive_resize(View& view, uint32_t edges) {
    if (view.state() != View::State::Normal) {
        return;
    }
    const wlr_box& c = view.content();
    const double border_x = c.x + ((edges & WLR_EDGE_RIGHT) != 0 ? c.width : 0);
    const double border_y = c.y + ((edges & WLR_EDGE_BOTTOM) != 0 ? c.height : 0);
    grab_ = {.mode = GrabMode::Resize,
             .view = &view,
             .x = cursor->x - border_x,
             .y = cursor->y - border_y,
             .box = c,
             .edges = edges};
    set_cursor_image(wlr_xcursor_get_resize_name(static_cast<wlr_edges>(edges)));
}

void Server::process_move() {
    View& view = *grab_.view;
    const int x = static_cast<int>(std::lround(cursor->x - grab_.x));
    int y = static_cast<int>(std::lround(cursor->y - grab_.y));
    // Never let the title bar go above the top of the screen.
    const wlr_box area = usable_area_at_cursor();
    if (!wlr_box_empty(&area)) {
        y = std::max(y, area.y + (view.content().y - view.frame().y));
    }
    view.move_to(x, y);
}

void Server::process_resize() {
    // Simplification: position changes immediately rather than when the
    // client commits a buffer at the new size.
    View& view = *grab_.view;
    const auto border_x = static_cast<int>(std::lround(cursor->x - grab_.x));
    const auto border_y = static_cast<int>(std::lround(cursor->y - grab_.y));
    int left = grab_.box.x;
    int right = grab_.box.x + grab_.box.width;
    int top = grab_.box.y;
    int bottom = grab_.box.y + grab_.box.height;

    if ((grab_.edges & WLR_EDGE_TOP) != 0) {
        top = std::min(border_y, bottom - kMinHeight);
    } else if ((grab_.edges & WLR_EDGE_BOTTOM) != 0) {
        bottom = std::max(border_y, top + kMinHeight);
    }
    if ((grab_.edges & WLR_EDGE_LEFT) != 0) {
        left = std::min(border_x, right - kMinWidth);
    } else if ((grab_.edges & WLR_EDGE_RIGHT) != 0) {
        right = std::max(border_x, left + kMinWidth);
    }
    view.move_to(left, top);
    view.request_size(right - left, bottom - top);
}

// --- key bindings --------------------------------------------------------------------

bool Server::take_screenshot(uint32_t mode) {
    if (!shell_protocol_) {
        return false; // let applications have the key
    }
    wlr_output* output = wlr_output_layout_output_at(output_layout, cursor->x, cursor->y);
    wlr_box window{};
    if (mode == TETRA_SHELL_V1_SCREENSHOT_MODE_WINDOW) {
        if (focused_view_ != nullptr && focused_view_->mapped() && !focused_view_->minimized()) {
            window = focused_view_->frame();
            output = wlr_output_layout_output_at(output_layout, window.x + (window.width / 2.0),
                                                 window.y + (window.height / 2.0));
        } else {
            mode = TETRA_SHELL_V1_SCREENSHOT_MODE_OUTPUT; // no window: the whole screen
        }
    }
    if (output == nullptr) {
        return true;
    }
    wlr_box area{};
    wlr_output_layout_get_box(output_layout, output, &area);
    wlr_box box{.x = 0, .y = 0, .width = area.width, .height = area.height};
    if (mode == TETRA_SHELL_V1_SCREENSHOT_MODE_WINDOW) {
        // The part of the window on that output, relative to it.
        wlr_box clipped{};
        if (!wlr_box_intersection(&clipped, &window, &area)) {
            return true;
        }
        box = {.x = clipped.x - area.x, .y = clipped.y - area.y, .width = clipped.width,
               .height = clipped.height};
    }
    shell_protocol_->screenshot(mode, output, box);
    return true;
}

bool Server::handle_keybinding(uint32_t modifiers, xkb_keysym_t sym) {
    // Ctrl+Alt+F<n> arrives as XF86Switch_VT_<n>.
    if (sym >= XKB_KEY_XF86Switch_VT_1 && sym <= XKB_KEY_XF86Switch_VT_12) {
        if (session != nullptr) {
            wlr_session_change_vt(session, sym - XKB_KEY_XF86Switch_VT_1 + 1);
        }
        return true;
    }

    // Volume and brightness keys go to the shell (which owns the OSD),
    // whatever modifiers are held.
    uint32_t media_key = 0;
    switch (sym) {
    case XKB_KEY_XF86AudioRaiseVolume:
        media_key = TETRA_SHELL_V1_MEDIA_KEY_VOLUME_UP;
        break;
    case XKB_KEY_XF86AudioLowerVolume:
        media_key = TETRA_SHELL_V1_MEDIA_KEY_VOLUME_DOWN;
        break;
    case XKB_KEY_XF86AudioMute:
        media_key = TETRA_SHELL_V1_MEDIA_KEY_MUTE;
        break;
    case XKB_KEY_XF86AudioMicMute:
        media_key = TETRA_SHELL_V1_MEDIA_KEY_MIC_MUTE;
        break;
    case XKB_KEY_XF86MonBrightnessUp:
        media_key = TETRA_SHELL_V1_MEDIA_KEY_BRIGHTNESS_UP;
        break;
    case XKB_KEY_XF86MonBrightnessDown:
        media_key = TETRA_SHELL_V1_MEDIA_KEY_BRIGHTNESS_DOWN;
        break;
    default:
        break;
    }
    if (media_key != 0) {
        // Without a shell to handle it, let applications have the key.
        return shell_protocol_ && shell_protocol_->media_key(media_key);
    }

    // While locked, every other key goes to the lock screen.
    if (locked()) {
        return false;
    }

    // Built-in bindings; configurable bindings come with the task bar.
    const bool super = (modifiers & WLR_MODIFIER_LOGO) != 0;
    const bool shift = (modifiers & WLR_MODIFIER_SHIFT) != 0;
    const bool alt = (modifiers & WLR_MODIFIER_ALT) != 0;
    if (super && (sym == XKB_KEY_l || sym == XKB_KEY_L)) {
        request_lock();
        return true;
    }
    if (super && (sym == XKB_KEY_i || sym == XKB_KEY_I)) {
        open_settings();
        return true;
    }
    // Screenshots: Print (output), Shift+Print or Super+Shift+S (region),
    // Alt+Print (window; with Alt held most layouts send Sys_Req).
    if (sym == XKB_KEY_Print || sym == XKB_KEY_Sys_Req) {
        const uint32_t mode = alt || sym == XKB_KEY_Sys_Req ? TETRA_SHELL_V1_SCREENSHOT_MODE_WINDOW
                              : shift                       ? TETRA_SHELL_V1_SCREENSHOT_MODE_REGION
                                                            : TETRA_SHELL_V1_SCREENSHOT_MODE_OUTPUT;
        return take_screenshot(mode);
    }
    if (super && shift && (sym == XKB_KEY_S || sym == XKB_KEY_s)) {
        return take_screenshot(TETRA_SHELL_V1_SCREENSHOT_MODE_REGION);
    }
    if (super && shift && sym == XKB_KEY_Escape) {
        terminate();
        return true;
    }
    if (super && sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12) {
        const std::size_t slot = sym - XKB_KEY_F1;
        if (shift) {
            taskbar_.launch(slot);
        } else {
            taskbar_.activate(slot);
        }
        return true;
    }
    if (super && sym == XKB_KEY_Return) {
        spawn(config_.terminal);
        return true;
    }
    if (alt && sym == XKB_KEY_Tab) {
        focus_next_view();
        return true;
    }
    if (alt && sym == XKB_KEY_F4) {
        if (focused_view_ != nullptr) {
            focused_view_->close();
        }
        return true;
    }
    if (super && sym == XKB_KEY_Up) {
        if (focused_view_ != nullptr) {
            focused_view_->set_maximized(true);
        }
        return true;
    }
    if (super && sym == XKB_KEY_Down) {
        if (focused_view_ != nullptr) {
            if (focused_view_->state() == View::State::Maximized) {
                focused_view_->set_maximized(false);
            } else {
                focused_view_->set_minimized(true);
            }
        }
        return true;
    }
    return false;
}

} // namespace tetra
