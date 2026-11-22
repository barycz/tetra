#pragma once

#include <wayland-server-core.h>

#include <functional>
#include <type_traits>
#include <utility>

namespace tetra {

// RAII wrapper around wl_listener that calls a C++ callable.
//
//   Listener map;
//   map.connect(surface->events.map, [this] { on_map(); });
//   Listener key;
//   key.connect<wlr_keyboard_key_event>(kb->events.key, [this](auto* e) { ... });
//
// The listener is removed from its signal on destruction or on the next
// connect(). The callback may destroy the Listener (and its owner) as the very
// last thing it does; wlroots emits signals with wl_signal_emit_mutable, which
// tolerates listeners being removed during emission.
//
// A Listener must not outlive the object owning the signal it is connected to:
// call disconnect() (or destroy the Listener) first.
class Listener {
public:
    Listener() = default;
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;
    Listener(Listener&&) = delete;
    Listener& operator=(Listener&&) = delete;
    ~Listener() { disconnect(); }

    template <typename Data = void, typename Fn>
    void connect(wl_signal& signal, Fn&& fn) {
        disconnect();
        if constexpr (std::is_void_v<Data>) {
            static_assert(std::is_invocable_v<Fn&>, "callback for Listener<void> takes no arguments");
            callback_ = [f = std::forward<Fn>(fn)](void*) mutable { f(); };
        } else {
            static_assert(std::is_invocable_v<Fn&, Data*>, "callback must accept Data*");
            callback_ = [f = std::forward<Fn>(fn)](void* data) mutable { f(static_cast<Data*>(data)); };
        }
        node_.owner = this;
        node_.listener.notify = &Listener::dispatch;
        wl_signal_add(&signal, &node_.listener);
        connected_ = true;
    }

    void disconnect() noexcept {
        if (connected_) {
            wl_list_remove(&node_.listener.link);
            connected_ = false;
        }
    }

    [[nodiscard]] bool connected() const noexcept { return connected_; }

private:
    // Standard-layout, so a wl_listener* can be converted back to Node*
    // without relying on offsetof() for a non-standard-layout class.
    struct Node {
        wl_listener listener{};
        Listener* owner = nullptr;
    };
    static_assert(std::is_standard_layout_v<Node>);

    static void dispatch(wl_listener* listener, void* data) {
        auto* node = reinterpret_cast<Node*>(listener); // NOLINT(*-reinterpret-cast)
        node->owner->callback_(data);
    }

    Node node_;
    std::move_only_function<void(void*)> callback_;
    bool connected_ = false;
};

} // namespace tetra
