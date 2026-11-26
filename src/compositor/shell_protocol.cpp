#include "shell_protocol.hpp"

#include "server.hpp"

#include "tetra-shell-unstable-v1-protocol.h"

#include <algorithm>

namespace tetra {

namespace {
constexpr int kVersion = 5;
}

struct ShellProtocolImpl {
    static ShellProtocol* from(wl_resource* resource) {
        return static_cast<ShellProtocol*>(wl_resource_get_user_data(resource));
    }

    static const struct tetra_shell_v1_interface impl;

    static void resource_destroyed(wl_resource* resource) {
        if (ShellProtocol* self = from(resource)) {
            std::erase(self->resources_, resource);
        }
    }
};

const struct tetra_shell_v1_interface ShellProtocolImpl::impl = {
    .destroy = [](wl_client* /*client*/, wl_resource* resource) { wl_resource_destroy(resource); },
    .activate_slot =
        [](wl_client* /*client*/, wl_resource* resource, uint32_t index) {
            if (ShellProtocol* self = from(resource)) {
                self->server_.taskbar().activate(index);
            }
        },
    .launch_slot =
        [](wl_client* /*client*/, wl_resource* resource, uint32_t index) {
            if (ShellProtocol* self = from(resource)) {
                self->server_.taskbar().launch(index);
            }
        },
    .take_screenshot =
        [](wl_client* /*client*/, wl_resource* resource, uint32_t mode) {
            ShellProtocol* self = from(resource);
            if (self != nullptr && mode >= TETRA_SHELL_V1_SCREENSHOT_MODE_OUTPUT &&
                mode <= TETRA_SHELL_V1_SCREENSHOT_MODE_WINDOW) {
                self->server_.take_screenshot(mode);
            }
        },
};

ShellProtocol::ShellProtocol(Server& server)
    : server_(server),
      global_(wl_global_create(server.display, &tetra_shell_v1_interface, kVersion, this, &bind)) {}

ShellProtocol::~ShellProtocol() {
    for (wl_resource* resource : resources_) {
        wl_resource_set_user_data(resource, nullptr);
    }
    wl_global_destroy(global_);
}

void ShellProtocol::bind(wl_client* client, void* data, uint32_t version, uint32_t id) {
    auto* self = static_cast<ShellProtocol*>(data);
    wl_resource* resource = wl_resource_create(client, &tetra_shell_v1_interface,
                                               static_cast<int>(version), id);
    if (resource == nullptr) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &ShellProtocolImpl::impl, self,
                                   &ShellProtocolImpl::resource_destroyed);
    self->resources_.push_back(resource);

    // Fresh shell (or restarted one): the full state.
    self->last_ = self->server_.taskbar().snapshot();
    for (std::size_t i = 0; i < self->last_.size(); ++i) {
        send_slot(resource, i, self->last_.at(i));
    }
    tetra_shell_v1_send_done(resource);

    // A lock client died while this shell was gone (or before it started).
    if (self->server_.lock_abandoned() &&
        wl_resource_get_version(resource) >= TETRA_SHELL_V1_LOCK_SINCE_VERSION) {
        tetra_shell_v1_send_lock(resource);
    }
}

void ShellProtocol::send_slot(wl_resource* resource, std::size_t index,
                              const Taskbar::SlotState& slot) {
    tetra_shell_v1_send_slot(resource, static_cast<uint32_t>(index), slot.app_id.c_str(),
                             slot.title.c_str(), slot.pinned ? 1 : 0, slot.windows, slot.state);
}

void ShellProtocol::publish(const Taskbar::Snapshot& snapshot) {
    bool changed = false;
    for (std::size_t i = 0; i < snapshot.size(); ++i) {
        if (snapshot.at(i) == last_.at(i)) {
            continue;
        }
        changed = true;
        for (wl_resource* resource : resources_) {
            send_slot(resource, i, snapshot.at(i));
        }
    }
    last_ = snapshot;
    if (changed) {
        for (wl_resource* resource : resources_) {
            tetra_shell_v1_send_done(resource);
        }
    }
}

void ShellProtocol::show_launcher() {
    for (wl_resource* resource : resources_) {
        tetra_shell_v1_send_show_launcher(resource);
    }
}

bool ShellProtocol::media_key(uint32_t key) {
    bool sent = false;
    for (wl_resource* resource : resources_) {
        if (wl_resource_get_version(resource) >= TETRA_SHELL_V1_MEDIA_KEY_SINCE_VERSION) {
            tetra_shell_v1_send_media_key(resource, key);
            sent = true;
        }
    }
    return sent;
}

bool ShellProtocol::screenshot(uint32_t mode, wlr_output* output, const wlr_box& box) {
    bool sent = false;
    for (wl_resource* resource : resources_) {
        if (wl_resource_get_version(resource) < TETRA_SHELL_V1_SCREENSHOT_SINCE_VERSION) {
            continue;
        }
        wl_client* client = wl_resource_get_client(resource);
        // The shell's own wl_output object for that output.
        wl_resource* output_resource = nullptr;
        wl_resource* candidate = nullptr;
        wl_resource_for_each(candidate, &output->resources) {
            if (wl_resource_get_client(candidate) == client) {
                output_resource = candidate;
                break;
            }
        }
        if (output_resource == nullptr) {
            continue;
        }
        // A serial the seat knows it gave this client: the shell may use it
        // to put the picture on the clipboard.
        wlr_seat_client* seat_client = wlr_seat_client_for_wl_client(server_.seat, client);
        const uint32_t serial = seat_client != nullptr ? wlr_seat_client_next_serial(seat_client)
                                                       : wl_display_next_serial(server_.display);
        tetra_shell_v1_send_screenshot(resource, serial, mode, output_resource, box.x, box.y,
                                       box.width, box.height);
        sent = true;
    }
    return sent;
}

bool ShellProtocol::lock() {
    bool sent = false;
    for (wl_resource* resource : resources_) {
        if (wl_resource_get_version(resource) >= TETRA_SHELL_V1_LOCK_SINCE_VERSION) {
            tetra_shell_v1_send_lock(resource);
            sent = true;
        }
    }
    return sent;
}

} // namespace tetra
