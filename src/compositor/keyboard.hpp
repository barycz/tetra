#pragma once

#include "listener.hpp"
#include "wlr.hpp"

namespace tetra {

class Server;

// A physical keyboard. All keyboards feed the single seat; compositor
// keybindings are intercepted before keys reach clients.
class Keyboard {
public:
    Keyboard(Server& server, wlr_input_device* device);

    Keyboard(const Keyboard&) = delete;
    Keyboard& operator=(const Keyboard&) = delete;
    Keyboard(Keyboard&&) = delete;
    Keyboard& operator=(Keyboard&&) = delete;
    ~Keyboard() = default;

private:
    void handle_key(const wlr_keyboard_key_event& event);

    Server& server_;
    wlr_keyboard* keyboard_;

    Listener modifiers_;
    Listener key_;
    Listener destroy_;
};

} // namespace tetra
