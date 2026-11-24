#pragma once

#include "config.hpp"
#include "listener.hpp"
#include "wlr.hpp"

namespace tetra {

class Server;

// A pointer device: a mouse, trackpoint or touchpad. All of them move the one
// cursor; this only holds the device's settings (speed, scrolling, tapping),
// which libinput devices take from config "mouse" and "touchpad". Virtual
// pointers and nested/headless backends have no such settings.
class Pointer {
public:
    Pointer(Server& server, wlr_input_device* device);

    Pointer(const Pointer&) = delete;
    Pointer& operator=(const Pointer&) = delete;
    Pointer(Pointer&&) = delete;
    Pointer& operator=(Pointer&&) = delete;
    ~Pointer() = default;

    void apply_config(const config::Mouse& mouse, const config::Touchpad& touchpad);
    [[nodiscard]] bool configurable() const noexcept { return libinput_ != nullptr; }

private:
    Server& server_;
    wlr_input_device* device_;
    libinput_device* libinput_ = nullptr; // null: not a libinput device
    bool touchpad_ = false;

    Listener destroy_;
};

} // namespace tetra
