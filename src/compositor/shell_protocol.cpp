#include "shell_protocol.hpp"

#include "server.hpp"

#include "tetra-shell-unstable-v1-protocol.h"

#include <algorithm>

namespace tetra {

namespace {
constexpr int kVersion = 1;
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

} // namespace tetra
