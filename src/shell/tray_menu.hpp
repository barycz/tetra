#pragma once

#include <sdbus-c++/sdbus-c++.h>

#include <imgui.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct wl_output;

namespace tetra::ui {
class LayerWindow;
}

namespace tetra::shell {

class Shell;

// A tray item's menu (com.canonical.dbusmenu), opened from the bar: read
// asynchronously, drawn above the bar at the icon, with submenus, check
// marks and radio items. A click sends the "clicked" event and closes it;
// Escape or a click outside closes it.
class TrayMenu {
public:
    TrayMenu(Shell& shell, sdbus::IConnection& bus, const std::string& service,
             const std::string& path, wl_output* output, float anchor_x);
    ~TrayMenu();

    TrayMenu(const TrayMenu&) = delete;
    TrayMenu& operator=(const TrayMenu&) = delete;
    TrayMenu(TrayMenu&&) = delete;
    TrayMenu& operator=(TrayMenu&&) = delete;

private:
    struct Node {
        int32_t id = 0;
        std::string label;
        bool enabled = true;
        bool visible = true;
        bool separator = false;
        enum class Toggle { None, Check, Radio } toggle = Toggle::None;
        bool checked = false;
        std::vector<Node> children;
    };
    using Layout = sdbus::Struct<int32_t, std::map<std::string, sdbus::Variant>, std::vector<sdbus::Variant>>;

    void fetch();
    static Node parse(const Layout& layout);
    void draw();
    void draw_items(const std::vector<Node>& nodes);
    void clicked(const Node& node);

    Shell& shell_;
    std::unique_ptr<sdbus::IProxy> proxy_;
    std::unique_ptr<ui::LayerWindow> window_;
    float anchor_x_;
    Node root_;
    bool loaded_ = false;
    bool failed_ = false;
    ImVec2 size_{};      // of the panel, from the last frame
    int frames_ = 0;
};

} // namespace tetra::shell
