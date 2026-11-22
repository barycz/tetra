#pragma once

#include "config.hpp"
#include "listener.hpp"
#include "wlr.hpp"

namespace tetra {

class Server;

// A keyboard. All keyboards feed the single seat; compositor keybindings are
// intercepted before keys reach clients.
class Keyboard {
public:
    Keyboard(Server& server, wlr_input_device* device);

    Keyboard(const Keyboard&) = delete;
    Keyboard& operator=(const Keyboard&) = delete;
    Keyboard(Keyboard&&) = delete;
    Keyboard& operator=(Keyboard&&) = delete;
    ~Keyboard() = default;

    // Keymap and repeat rate. Virtual keyboards keep their client's keymap.
    void apply_config(const config::Keyboard& config);
    void cancel_super_tap() noexcept { super_tap_ = false; }

private:
    void handle_key(const wlr_keyboard_key_event& event);

    Server& server_;
    wlr_keyboard* keyboard_;
    bool is_virtual_;
    bool super_tap_ = false; // Super pressed alone and nothing else since

    Listener modifiers_;
    Listener key_;
    Listener destroy_;
};

} // namespace tetra
