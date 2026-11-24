// tetra-tray-item: a StatusNotifierItem with a dbusmenu, for headless
// scenarios. What it receives (Activate, menu clicks, scrolls) is reported
// as a desktop notification, so it shows up in screenshots.
//
//   tetra-tray-item NAME (--icon ICON_NAME | --pixmap RRGGBB) [--appindicator]
//
// --appindicator: like libappindicator, no Activate method (a left click
// should open the menu) and registration by object path.

#include <sdbus-c++/sdbus-c++.h>

#include <unistd.h>

#include <charconv>
#include <cstdint>
#include <map>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Pixmap = sdbus::Struct<int32_t, int32_t, std::vector<uint8_t>>;
using Layout = sdbus::Struct<int32_t, std::map<std::string, sdbus::Variant>, std::vector<sdbus::Variant>>;
using ToolTip = sdbus::Struct<std::string, std::vector<Pixmap>, std::string, std::string>;

constexpr const char* kItem = "org.kde.StatusNotifierItem";
constexpr const char* kMenu = "com.canonical.dbusmenu";

// A filled square with a light border, ARGB big-endian.
std::vector<Pixmap> make_pixmap(uint32_t rgb) {
    std::vector<Pixmap> out;
    for (const int size : {16, 32}) {
        std::vector<uint8_t> data;
        data.reserve(static_cast<std::size_t>(size) * size * 4);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const bool border = x < size / 8 || y < size / 8 || x >= size - (size / 8) ||
                                    y >= size - (size / 8);
                data.push_back(0xff);
                data.push_back(border ? 0xee : static_cast<uint8_t>(rgb >> 16));
                data.push_back(border ? 0xee : static_cast<uint8_t>(rgb >> 8));
                data.push_back(border ? 0xee : static_cast<uint8_t>(rgb));
            }
        }
        out.emplace_back(size, size, std::move(data));
    }
    return out;
}

struct MenuEntry {
    int32_t id;
    std::string label;
    std::string type;   // "" or "separator"
    std::string toggle; // "", "checkmark"
    int32_t state = 0;
    bool enabled = true;
    std::vector<MenuEntry> children;
};

Layout to_layout(const MenuEntry& entry) {
    std::map<std::string, sdbus::Variant> props;
    if (!entry.label.empty()) {
        props.emplace("label", sdbus::Variant(entry.label));
    }
    if (!entry.type.empty()) {
        props.emplace("type", sdbus::Variant(entry.type));
    }
    if (!entry.toggle.empty()) {
        props.emplace("toggle-type", sdbus::Variant(entry.toggle));
        props.emplace("toggle-state", sdbus::Variant(entry.state));
    }
    if (!entry.enabled) {
        props.emplace("enabled", sdbus::Variant(false));
    }
    if (!entry.children.empty()) {
        props.emplace("children-display", sdbus::Variant(std::string("submenu")));
    }
    std::vector<sdbus::Variant> children;
    for (const auto& child : entry.children) {
        children.emplace_back(to_layout(child));
    }
    return Layout{entry.id, std::move(props), std::move(children)};
}

const MenuEntry* find(const MenuEntry& entry, int32_t id) {
    if (entry.id == id) {
        return &entry;
    }
    for (const auto& child : entry.children) {
        if (const MenuEntry* found = find(child, id)) {
            return found;
        }
    }
    return nullptr;
}

} // namespace

int main(int argc, char* argv[]) {
    const auto args = std::span(argv, static_cast<std::size_t>(argc)).subspan(1);
    std::string name;
    std::string icon;
    uint32_t color = 0;
    bool pixmap = false;
    bool appindicator = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "--icon" && i + 1 < args.size()) {
            icon = args[++i];
        } else if (arg == "--pixmap" && i + 1 < args.size()) {
            const std::string_view hex = args[++i];
            std::from_chars(hex.data(), hex.data() + hex.size(), color, 16);
            pixmap = true;
        } else if (arg == "--appindicator") {
            appindicator = true;
        } else if (name.empty()) {
            name = arg;
        }
    }
    if (name.empty() || (icon.empty() && !pixmap)) {
        std::println(stderr, "usage: tetra-tray-item NAME (--icon NAME | --pixmap RRGGBB) [--appindicator]");
        return 2;
    }

    auto connection = sdbus::createSessionBusConnection();
    auto notify = [&](const std::string& body) {
        auto proxy = sdbus::createProxy(*connection, sdbus::ServiceName{"org.freedesktop.Notifications"},
                                        sdbus::ObjectPath{"/org/freedesktop/Notifications"});
        try {
            proxy->callMethod("Notify")
                .onInterface("org.freedesktop.Notifications")
                .withArguments(std::string("tetra-tray-item"), uint32_t{0}, std::string(), name, body,
                               std::vector<std::string>{}, std::map<std::string, sdbus::Variant>{},
                               int32_t{-1});
        } catch (const sdbus::Error& e) {
            std::println(stderr, "tetra-tray-item: {}", e.what());
        }
    };

    MenuEntry menu{0, "", "", "", 0, true, {
        {1, "_Open window", "", "", 0, true, {}},
        {2, "", "separator", "", 0, true, {}},
        {3, "Show _details", "", "checkmark", 1, true, {}},
        {4, "_More", "", "", 0, true, {
            {5, "Item _A", "", "", 0, true, {}},
            {6, "Disabled item", "", "", 0, false, {}},
        }},
        {7, "_Quit", "", "", 0, true, {}},
    }};

    const std::string item_path = appindicator ? "/org/ayatana/NotificationItem/" + name : "/StatusNotifierItem";
    auto item = sdbus::createObject(*connection, sdbus::ObjectPath{item_path});
    const std::vector<Pixmap> pixmaps = pixmap ? make_pixmap(color) : std::vector<Pixmap>{};
    auto vtable = std::vector<sdbus::VTableItem>{
        sdbus::registerProperty("Category").withGetter([] { return std::string("ApplicationStatus"); }),
        sdbus::registerProperty("Id").withGetter([&] { return name; }),
        sdbus::registerProperty("Title").withGetter([&] { return name; }),
        sdbus::registerProperty("Status").withGetter([] { return std::string("Active"); }),
        sdbus::registerProperty("IconName").withGetter([&] { return icon; }),
        sdbus::registerProperty("IconPixmap").withGetter([&] { return pixmaps; }),
        sdbus::registerProperty("IconThemePath").withGetter([] { return std::string(); }),
        sdbus::registerProperty("ToolTip").withGetter([&] { return ToolTip{std::string(), {}, name, std::string()}; }),
        sdbus::registerProperty("ItemIsMenu").withGetter([] { return false; }),
        sdbus::registerProperty("Menu").withGetter([] { return sdbus::ObjectPath{"/MenuBar"}; }),
        sdbus::registerMethod("ContextMenu").implementedAs([&](int32_t, int32_t) { notify("context menu"); }),
        sdbus::registerMethod("SecondaryActivate").implementedAs([&](int32_t, int32_t) { notify("secondary activate"); }),
        sdbus::registerMethod("Scroll").implementedAs([&](int32_t delta, const std::string& orientation) {
            notify("scroll " + std::to_string(delta) + " " + orientation);
        }),
        sdbus::registerSignal("NewIcon"),
        sdbus::registerSignal("NewStatus").withParameters<std::string>("status"),
    };
    if (!appindicator) {
        vtable.push_back(sdbus::registerMethod("Activate").implementedAs([&](int32_t x, int32_t y) {
            notify("activate at " + std::to_string(x) + "," + std::to_string(y));
        }));
    }
    item->addVTable(std::move(vtable)).forInterface(sdbus::InterfaceName{kItem});

    auto menu_object = sdbus::createObject(*connection, sdbus::ObjectPath{"/MenuBar"});
    menu_object
        ->addVTable(
            sdbus::registerMethod("GetLayout").implementedAs([&](int32_t parent, int32_t /*depth*/,
                                                                 const std::vector<std::string>& /*props*/) {
                const MenuEntry* entry = find(menu, parent);
                return std::tuple<uint32_t, Layout>{1, to_layout(entry != nullptr ? *entry : menu)};
            }),
            sdbus::registerMethod("Event").implementedAs([&](int32_t id, const std::string& event,
                                                             const sdbus::Variant& /*data*/, uint32_t /*time*/) {
                if (event != "clicked") {
                    return;
                }
                const MenuEntry* entry = find(menu, id);
                notify("menu: " + (entry != nullptr ? entry->label : std::to_string(id)));
                if (id == 7) {
                    connection->leaveEventLoop();
                }
            }),
            sdbus::registerMethod("AboutToShow").implementedAs([](int32_t /*id*/) { return false; }),
            sdbus::registerProperty("Version").withGetter([] { return uint32_t{3}; }),
            sdbus::registerProperty("Status").withGetter([] { return std::string("normal"); }),
            sdbus::registerSignal("LayoutUpdated").withParameters<uint32_t, int32_t>("revision", "parent"))
        .forInterface(sdbus::InterfaceName{kMenu});

    std::string service = "org.kde.StatusNotifierItem-" + std::to_string(getpid()) + "-1";
    if (!appindicator) {
        connection->requestName(sdbus::ServiceName{service});
    }
    auto watcher = sdbus::createProxy(*connection, sdbus::ServiceName{"org.kde.StatusNotifierWatcher"},
                                      sdbus::ObjectPath{"/StatusNotifierWatcher"});
    try {
        watcher->callMethod("RegisterStatusNotifierItem")
            .onInterface("org.kde.StatusNotifierWatcher")
            .withArguments(appindicator ? item_path : service);
    } catch (const sdbus::Error& e) {
        std::println(stderr, "tetra-tray-item: cannot register: {}", e.what());
        return 1;
    }
    try {
        connection->enterEventLoop();
    } catch (const sdbus::Error&) {
        // the test bus went away at the end of the session
    }
    return 0;
}
