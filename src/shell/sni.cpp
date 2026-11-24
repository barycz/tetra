#include "sni.hpp"

#include "app.hpp"
#include "texture.hpp"

#include <algorithm>
#include <cstdlib>
#include <print>

#include <unistd.h>

namespace tetra::shell {

namespace {

constexpr const char* kWatcherName = "org.kde.StatusNotifierWatcher";
constexpr const char* kWatcherPath = "/StatusNotifierWatcher";
constexpr const char* kItemInterface = "org.kde.StatusNotifierItem";
constexpr const char* kDefaultItemPath = "/StatusNotifierItem";

using PixmapList = std::vector<sdbus::Struct<int32_t, int32_t, std::vector<uint8_t>>>;
using ToolTip = sdbus::Struct<std::string, PixmapList, std::string, std::string>;

template <typename T>
bool read(const std::map<sdbus::PropertyName, sdbus::Variant>& props, const char* key, T& out) {
    auto it = props.find(sdbus::PropertyName{key});
    if (it == props.end() || !it->second.containsValueOfType<T>()) {
        return false;
    }
    out = it->second.get<T>();
    return true;
}

std::vector<SniItem::Pixmap> read_pixmaps(const std::map<sdbus::PropertyName, sdbus::Variant>& props,
                                          const char* key) {
    std::vector<SniItem::Pixmap> out;
    PixmapList list;
    if (!read(props, key, list)) {
        return out;
    }
    for (auto& entry : list) {
        const int width = entry.get<0>();
        const int height = entry.get<1>();
        auto& data = entry.get<2>();
        if (width > 0 && height > 0 && data.size() == static_cast<std::size_t>(width) * height * 4) {
            out.push_back({width, height, std::move(data)});
        }
    }
    return out;
}

} // namespace

SniItem::SniItem() = default;
SniItem::~SniItem() = default;

const SniItem::Pixmap* SniItem::best_pixmap(int size) const {
    const auto& list = needs_attention() && !attention_pixmaps.empty() ? attention_pixmaps : icon_pixmaps;
    const Pixmap* best = nullptr;
    for (const auto& p : list) {
        // The smallest one at least `size` wide, else the biggest.
        if (best == nullptr || (best->width < size && p.width > best->width) ||
            (p.width >= size && p.width < best->width)) {
            best = &p;
        }
    }
    return best;
}

SniTray::SniTray(ui::App& app, sdbus::IConnection& session_bus) : app_(app), bus_(session_bus) {
    watcher_ = sdbus::createObject(bus_, sdbus::ObjectPath{kWatcherPath});
    watcher_
        ->addVTable(
            sdbus::registerMethod("RegisterStatusNotifierItem")
                .withInputParamNames("service")
                .implementedAs([this](const std::string& service) {
                    const sdbus::Message message = watcher_->getCurrentlyProcessedMessage();
                    const char* sender = message.getSender();
                    register_item(sender != nullptr ? sender : "", service);
                }),
            sdbus::registerMethod("RegisterStatusNotifierHost")
                .withInputParamNames("service")
                .implementedAs([](const std::string& /*service*/) {}), // we are the host
            sdbus::registerProperty("RegisteredStatusNotifierItems").withGetter([this] {
                return registered();
            }),
            sdbus::registerProperty("IsStatusNotifierHostRegistered").withGetter([] { return true; }),
            sdbus::registerProperty("ProtocolVersion").withGetter([] { return int32_t{0}; }),
            sdbus::registerSignal("StatusNotifierItemRegistered").withParameters<std::string>("service"),
            sdbus::registerSignal("StatusNotifierItemUnregistered").withParameters<std::string>("service"),
            sdbus::registerSignal("StatusNotifierHostRegistered"),
            sdbus::registerSignal("StatusNotifierHostUnregistered"))
        .forInterface(sdbus::InterfaceName{kWatcherName});

    dbus_ = sdbus::createProxy(bus_, sdbus::ServiceName{"org.freedesktop.DBus"},
                               sdbus::ObjectPath{"/org/freedesktop/DBus"});
    dbus_->uponSignal("NameOwnerChanged")
        .onInterface("org.freedesktop.DBus")
        .call([this](const std::string& name, const std::string& /*old_owner*/,
                     const std::string& new_owner) {
            if (new_owner.empty()) {
                owner_gone(name);
            }
        });

    bus_.requestName(sdbus::ServiceName{kWatcherName}); // throws if taken
    // Some apps also look for a host name; we are it.
    try {
        bus_.requestName(sdbus::ServiceName{"org.kde.StatusNotifierHost-" + std::to_string(getpid())});
    } catch (const sdbus::Error&) {
        // harmless
    }
    // Apps that started before us (e.g. a restarted shell) re-register
    // when they see this.
    watcher_->emitSignal("StatusNotifierHostRegistered").onInterface(kWatcherName);
}

SniTray::~SniTray() {
    app_.make_gl_current(); // item textures
    items_.clear();
    try {
        bus_.releaseName(sdbus::ServiceName{kWatcherName});
    } catch (const sdbus::Error&) {
        // the connection may already be closing
    }
}

std::vector<std::string> SniTray::registered() const {
    std::vector<std::string> ids;
    ids.reserve(items_.size());
    for (const auto& item : items_) {
        ids.push_back(item->id);
    }
    return ids;
}

void SniTray::register_item(const std::string& sender, const std::string& service) {
    // Either a bus name (the path is the default) or, from libappindicator,
    // an object path on the sender's connection.
    std::string name = service;
    std::string path = kDefaultItemPath;
    if (service.starts_with('/')) {
        name = sender;
        path = service;
    } else if (service.empty()) {
        name = sender;
    }
    if (name.empty()) {
        return;
    }
    const std::string id = name + path;
    if (std::ranges::any_of(items_, [&](const auto& item) { return item->id == id; })) {
        return;
    }
    auto item = std::make_unique<SniItem>();
    item->id = id;
    item->service = name;
    item->path = path;
    try {
        item->proxy = sdbus::createProxy(bus_, sdbus::ServiceName{name}, sdbus::ObjectPath{path});
    } catch (const sdbus::Error& e) {
        std::println(stderr, "tetra-shell: tray item {}: {}", id, e.what());
        return;
    }
    SniItem* raw = item.get();
    // Any change: read everything again (cheap, and robust to apps that
    // send the wrong signal).
    for (const char* signal : {"NewTitle", "NewIcon", "NewAttentionIcon", "NewOverlayIcon",
                               "NewToolTip", "NewIconThemePath", "NewMenu"}) {
        raw->proxy->uponSignal(signal).onInterface(kItemInterface).call([this, raw] { refresh(*raw); });
    }
    raw->proxy->uponSignal("NewStatus").onInterface(kItemInterface).call([this, raw](const std::string& status) {
        raw->status = status;
        changed();
    });
    items_.push_back(std::move(item));
    watcher_->emitSignal("StatusNotifierItemRegistered").onInterface(kWatcherName).withArguments(id);
    refresh(*raw);
}

void SniTray::owner_gone(const std::string& name) {
    // Note the ids first: remove_if leaves moved-from (null) entries behind.
    std::vector<std::string> ids;
    for (const auto& item : items_) {
        if (item->service == name) {
            ids.push_back(item->id);
        }
    }
    if (ids.empty()) {
        return;
    }
    app_.make_gl_current(); // their textures
    std::erase_if(items_, [&](const auto& item) { return item->service == name; });
    for (const auto& id : ids) {
        watcher_->emitSignal("StatusNotifierItemUnregistered").onInterface(kWatcherName).withArguments(id);
    }
    changed();
}

void SniTray::refresh(SniItem& item) {
    // The reply handler belongs to the item's proxy: if the item goes away
    // first, the call is cancelled with it.
    SniItem* raw = &item;
    item.proxy->getAllPropertiesAsync().onInterface(kItemInterface).uponReplyInvoke(
        [this, raw](std::optional<sdbus::Error> error,
                    std::map<sdbus::PropertyName, sdbus::Variant> props) {
            if (error) {
                std::println(stderr, "tetra-shell: tray item {}: {}", raw->id, error->what());
                return;
            }
            read(props, "Title", raw->title);
            read(props, "Status", raw->status);
            read(props, "IconName", raw->icon_name);
            read(props, "AttentionIconName", raw->attention_icon_name);
            read(props, "IconThemePath", raw->icon_theme_path);
            read(props, "ItemIsMenu", raw->item_is_menu);
            sdbus::ObjectPath menu;
            raw->menu_path = read(props, "Menu", menu) && menu != "/" ? std::string(menu) : std::string();
            ToolTip tooltip;
            raw->tooltip = read(props, "ToolTip", tooltip) ? tooltip.get<2>() : std::string();
            raw->icon_pixmaps = read_pixmaps(props, "IconPixmap");
            raw->attention_pixmaps = read_pixmaps(props, "AttentionIconPixmap");
            raw->texture_dirty = true;
            raw->ready = true;
            changed();
        });
}

void SniTray::changed() {
    if (on_changed) {
        on_changed();
    }
}

void SniTray::activate(SniItem& item, int x, int y, std::function<void()> on_menu) {
    if (item.item_is_menu && !item.menu_path.empty()) {
        on_menu();
        return;
    }
    // libappindicator items have no Activate: show their menu instead.
    const bool has_menu = !item.menu_path.empty();
    item.proxy->callMethodAsync("Activate")
        .onInterface(kItemInterface)
        .withArguments(int32_t{x}, int32_t{y})
        .uponReplyInvoke([has_menu, on_menu = std::move(on_menu)](std::optional<sdbus::Error> error) {
            if (error && has_menu) {
                on_menu();
            }
        });
}

void SniTray::secondary_activate(SniItem& item, int x, int y) {
    item.proxy->callMethodAsync("SecondaryActivate")
        .onInterface(kItemInterface)
        .withArguments(int32_t{x}, int32_t{y})
        .uponReplyInvoke([](std::optional<sdbus::Error> /*error*/) {});
}

void SniTray::context_menu(SniItem& item, int x, int y) {
    item.proxy->callMethodAsync("ContextMenu")
        .onInterface(kItemInterface)
        .withArguments(int32_t{x}, int32_t{y})
        .uponReplyInvoke([](std::optional<sdbus::Error> /*error*/) {});
}

void SniTray::scroll(SniItem& item, int delta) {
    item.proxy->callMethodAsync("Scroll")
        .onInterface(kItemInterface)
        .withArguments(int32_t{delta}, std::string("vertical"))
        .uponReplyInvoke([](std::optional<sdbus::Error> /*error*/) {});
}

} // namespace tetra::shell
