#include "clipboard.hpp"

#include "app.hpp"
#include "protocols.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <string_view>

#include <fcntl.h>
#include <unistd.h>

namespace tetra::ui {

namespace {

// Most specific first; what we offer and what we accept.
constexpr std::array<const char*, 5> kTextTypes = {"text/plain;charset=utf-8", "UTF8_STRING", "text/plain",
                                                   "TEXT", "STRING"};
constexpr std::size_t kMaxText = 16U << 20U; // more than this is not text worth pasting

constexpr std::size_t index(Clipboard::Selection which) {
    return static_cast<std::size_t>(which);
}

} // namespace

struct ClipboardListeners {
    static const wl_data_offer_listener data_offer;
    static const wl_data_device_listener data_device;
    static const wl_data_source_listener data_source;
    static const zwp_primary_selection_offer_v1_listener primary_offer;
    static const zwp_primary_selection_device_v1_listener primary_device;
    static const zwp_primary_selection_source_v1_listener primary_source;

    static void add_mime_type(void* data, void* proxy, const char* mime_type) {
        if (auto* offer = static_cast<Clipboard*>(data)->find_offer(proxy)) {
            offer->mime_types.emplace_back(mime_type);
        }
    }
    static void new_offer(Clipboard* self, void* proxy, Clipboard::Selection kind) {
        self->offers_.push_back({.proxy = proxy, .kind = kind, .mime_types = {}});
    }
};

const wl_data_offer_listener ClipboardListeners::data_offer = {
    .offer = [](void* data, wl_data_offer* offer, const char* mime_type) { add_mime_type(data, offer, mime_type); },
    .source_actions = [](void* /*data*/, wl_data_offer* /*offer*/, uint32_t /*actions*/) {},
    .action = [](void* /*data*/, wl_data_offer* /*offer*/, uint32_t /*action*/) {},
};

const wl_data_device_listener ClipboardListeners::data_device = {
    .data_offer =
        [](void* data, wl_data_device* /*device*/, wl_data_offer* offer) {
            auto* self = static_cast<Clipboard*>(data);
            new_offer(self, offer, Clipboard::Selection::Clipboard);
            wl_data_offer_add_listener(offer, &ClipboardListeners::data_offer, self);
        },
    // Drag and drop is not taken: its offers are dropped when it ends.
    .enter = [](void* /*data*/, wl_data_device* /*device*/, uint32_t /*serial*/, wl_surface* /*surface*/,
                wl_fixed_t /*x*/, wl_fixed_t /*y*/, wl_data_offer* /*offer*/) {},
    .leave = [](void* /*data*/, wl_data_device* /*device*/) {},
    .motion = [](void* /*data*/, wl_data_device* /*device*/, uint32_t /*time*/, wl_fixed_t /*x*/,
                 wl_fixed_t /*y*/) {},
    .drop = [](void* /*data*/, wl_data_device* /*device*/) {},
    .selection =
        [](void* data, wl_data_device* /*device*/, wl_data_offer* offer) {
            static_cast<Clipboard*>(data)->take_selection(Clipboard::Selection::Clipboard, offer);
        },
};

const wl_data_source_listener ClipboardListeners::data_source = {
    .target = [](void* /*data*/, wl_data_source* /*source*/, const char* /*mime_type*/) {},
    .send =
        [](void* data, wl_data_source* /*source*/, const char* mime_type, int32_t fd) {
            static_cast<Clipboard*>(data)->send(Clipboard::Selection::Clipboard, mime_type, fd);
        },
    .cancelled =
        [](void* data, wl_data_source* /*source*/) {
            static_cast<Clipboard*>(data)->destroy_source(Clipboard::Selection::Clipboard);
        },
    .dnd_drop_performed = [](void* /*data*/, wl_data_source* /*source*/) {},
    .dnd_finished = [](void* /*data*/, wl_data_source* /*source*/) {},
    .action = [](void* /*data*/, wl_data_source* /*source*/, uint32_t /*action*/) {},
};

const zwp_primary_selection_offer_v1_listener ClipboardListeners::primary_offer = {
    .offer = [](void* data, zwp_primary_selection_offer_v1* offer,
                const char* mime_type) { add_mime_type(data, offer, mime_type); },
};

const zwp_primary_selection_device_v1_listener ClipboardListeners::primary_device = {
    .data_offer =
        [](void* data, zwp_primary_selection_device_v1* /*device*/, zwp_primary_selection_offer_v1* offer) {
            auto* self = static_cast<Clipboard*>(data);
            new_offer(self, offer, Clipboard::Selection::Primary);
            zwp_primary_selection_offer_v1_add_listener(offer, &ClipboardListeners::primary_offer, self);
        },
    .selection =
        [](void* data, zwp_primary_selection_device_v1* /*device*/, zwp_primary_selection_offer_v1* offer) {
            static_cast<Clipboard*>(data)->take_selection(Clipboard::Selection::Primary, offer);
        },
};

const zwp_primary_selection_source_v1_listener ClipboardListeners::primary_source = {
    .send =
        [](void* data, zwp_primary_selection_source_v1* /*source*/, const char* mime_type, int32_t fd) {
            static_cast<Clipboard*>(data)->send(Clipboard::Selection::Primary, mime_type, fd);
        },
    .cancelled =
        [](void* data, zwp_primary_selection_source_v1* /*source*/) {
            static_cast<Clipboard*>(data)->destroy_source(Clipboard::Selection::Primary);
        },
};

Clipboard::Clipboard(App& app, wl_seat* seat, wl_data_device_manager* data_manager,
                     zwp_primary_selection_device_manager_v1* primary_manager)
    : app_(app), data_manager_(data_manager), primary_manager_(primary_manager) {
    // A reader that goes away mid-paste must not kill us.
    std::signal(SIGPIPE, SIG_IGN); // NOLINT(cert-err33-c)
    if (data_manager_ != nullptr && seat != nullptr) {
        data_device_ = wl_data_device_manager_get_data_device(data_manager_, seat);
        wl_data_device_add_listener(data_device_, &ClipboardListeners::data_device, this);
    }
    if (primary_manager_ != nullptr && seat != nullptr) {
        primary_device_ = zwp_primary_selection_device_manager_v1_get_device(primary_manager_, seat);
        zwp_primary_selection_device_v1_add_listener(primary_device_, &ClipboardListeners::primary_device, this);
    }
}

Clipboard::~Clipboard() {
    for (const auto which : {Selection::Clipboard, Selection::Primary}) {
        finish_read(which, false);
        destroy_source(which);
    }
    for (const auto& offer : offers_) {
        destroy_offer(offer);
    }
    if (data_device_ != nullptr) {
        wl_data_device_release(data_device_);
    }
    if (primary_device_ != nullptr) {
        zwp_primary_selection_device_v1_destroy(primary_device_);
    }
}

Clipboard::Offer* Clipboard::find_offer(void* proxy) {
    auto it = std::ranges::find(offers_, proxy, &Offer::proxy);
    return it == offers_.end() ? nullptr : &*it;
}

void Clipboard::destroy_offer(const Offer& offer) {
    if (offer.kind == Selection::Clipboard) {
        wl_data_offer_destroy(static_cast<wl_data_offer*>(offer.proxy));
    } else {
        zwp_primary_selection_offer_v1_destroy(static_cast<zwp_primary_selection_offer_v1*>(offer.proxy));
    }
}

void Clipboard::take_selection(Selection which, void* offer) {
    Slot& slot = slots_.at(index(which));
    finish_read(which, false);
    slot.offer = offer;
    // The previous selection's offer, and any other this device announced
    // (drag and drop, which we do not take), are done with.
    for (auto it = offers_.begin(); it != offers_.end();) {
        if (it->kind == which && it->proxy != offer) {
            destroy_offer(*it);
            it = offers_.erase(it);
        } else {
            ++it;
        }
    }
    if (slot.source != nullptr) {
        return; // our own selection: its text is known
    }
    if (offer == nullptr) {
        if (!slot.text.empty()) {
            slot.text.clear();
            if (on_changed) {
                on_changed(which);
            }
        }
        return;
    }
    if (const Offer* known = find_offer(offer)) {
        read(which, *known);
    }
}

void Clipboard::read(Selection which, const Offer& offer) {
    const char* type = nullptr;
    for (const char* t : kTextTypes) {
        if (std::ranges::contains(offer.mime_types, std::string_view(t))) {
            type = t;
            break;
        }
    }
    if (type == nullptr) {
        return; // not text (an image, files): nothing to paste
    }
    std::array<int, 2> fds{};
    if (pipe2(fds.data(), O_CLOEXEC | O_NONBLOCK) != 0) {
        return;
    }
    if (which == Selection::Clipboard) {
        wl_data_offer_receive(static_cast<wl_data_offer*>(offer.proxy), type, fds[1]);
    } else {
        zwp_primary_selection_offer_v1_receive(static_cast<zwp_primary_selection_offer_v1*>(offer.proxy), type,
                                               fds[1]);
    }
    close(fds[1]);
    wl_display_flush(app_.display());
    Slot& slot = slots_.at(index(which));
    slot.reading_fd = fds[0];
    slot.incoming.clear();
    app_.watch_fd(fds[0], [this, which] {
        Slot& s = slots_.at(index(which));
        std::array<char, 65536> buffer{};
        for (;;) {
            const ssize_t n = ::read(s.reading_fd, buffer.data(), buffer.size());
            if (n > 0) {
                s.incoming.append(buffer.data(), static_cast<std::size_t>(n));
                if (s.incoming.size() > kMaxText) {
                    finish_read(which, false);
                    return;
                }
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
                return; // more later
            }
            finish_read(which, n == 0);
            return;
        }
    });
}

void Clipboard::finish_read(Selection which, bool complete) {
    Slot& slot = slots_.at(index(which));
    if (slot.reading_fd < 0) {
        return;
    }
    app_.unwatch_fd(slot.reading_fd);
    close(slot.reading_fd);
    slot.reading_fd = -1;
    if (complete && slot.incoming != slot.text) {
        slot.text = std::move(slot.incoming);
        if (on_changed) {
            on_changed(which);
        }
    }
    slot.incoming.clear();
}

void Clipboard::set(Selection which, std::string text, uint32_t serial) {
    Slot& slot = slots_.at(index(which));
    finish_read(which, false);
    destroy_source(which);
    slot.text = std::move(text);
    if (which == Selection::Clipboard && data_device_ != nullptr) {
        wl_data_source* source = wl_data_device_manager_create_data_source(data_manager_);
        for (const char* type : kTextTypes) {
            wl_data_source_offer(source, type);
        }
        wl_data_source_add_listener(source, &ClipboardListeners::data_source, this);
        wl_data_device_set_selection(data_device_, source, serial);
        slot.source = source;
    } else if (which == Selection::Primary && primary_device_ != nullptr) {
        zwp_primary_selection_source_v1* source = zwp_primary_selection_device_manager_v1_create_source(primary_manager_);
        for (const char* type : kTextTypes) {
            zwp_primary_selection_source_v1_offer(source, type);
        }
        zwp_primary_selection_source_v1_add_listener(source, &ClipboardListeners::primary_source, this);
        zwp_primary_selection_device_v1_set_selection(primary_device_, source, serial);
        slot.source = source;
    }
    wl_display_flush(app_.display());
}

void Clipboard::destroy_source(Selection which) {
    Slot& slot = slots_.at(index(which));
    if (slot.source == nullptr) {
        return;
    }
    if (which == Selection::Clipboard) {
        wl_data_source_destroy(static_cast<wl_data_source*>(slot.source));
    } else {
        zwp_primary_selection_source_v1_destroy(static_cast<zwp_primary_selection_source_v1*>(slot.source));
    }
    slot.source = nullptr;
}

void Clipboard::send(Selection which, const char* /*mime_type*/, int fd) const {
    // Blocking writes: the reader is another process reading as we write.
    const int flags = fcntl(fd, F_GETFL);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK); // NOLINT(hicpp-signed-bitwise)
    }
    const std::string& text = slots_.at(index(which)).text;
    std::size_t written = 0;
    while (written < text.size()) {
        const ssize_t n = write(fd, text.data() + written, text.size() - written);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        written += static_cast<std::size_t>(n);
    }
    close(fd);
}

} // namespace tetra::ui
