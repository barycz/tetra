#pragma once

#include "taskbar.hpp"
#include "wlr.hpp"

#include <vector>

namespace tetra {

class Server;

// Server side of tetra-shell-unstable-v1: publishes the task bar's slots to
// the shell and executes its requests.
class ShellProtocol {
public:
    explicit ShellProtocol(Server& server);
    ~ShellProtocol();

    ShellProtocol(const ShellProtocol&) = delete;
    ShellProtocol& operator=(const ShellProtocol&) = delete;
    ShellProtocol(ShellProtocol&&) = delete;
    ShellProtocol& operator=(ShellProtocol&&) = delete;

    [[nodiscard]] const wl_global* global() const noexcept { return global_; }

    void publish(const Taskbar::Snapshot& snapshot);
    void show_launcher();
    // Forwards a volume/brightness key (tetra_shell_v1.media_key); false if
    // no shell can take it.
    bool media_key(uint32_t key);

private:
    static void bind(wl_client* client, void* data, uint32_t version, uint32_t id);
    static void send_slot(wl_resource* resource, std::size_t index, const Taskbar::SlotState& slot);

    friend struct ShellProtocolImpl;
    Server& server_;
    wl_global* global_;
    std::vector<wl_resource*> resources_;
    Taskbar::Snapshot last_;
};

} // namespace tetra
