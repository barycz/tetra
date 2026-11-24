#include "tray_menu.hpp"

#include "bar.hpp"
#include "shell.hpp"
#include "style.hpp"
#include "window.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <print>

namespace tetra::shell {

namespace {

constexpr const char* kMenuInterface = "com.canonical.dbusmenu";
constexpr float kMinWidth = 200;

// "_Open" -> "Open"; "__" is a literal underscore.
std::string strip_mnemonic(const std::string& label) {
    std::string out;
    out.reserve(label.size());
    for (std::size_t i = 0; i < label.size(); ++i) {
        if (label[i] == '_') {
            if (i + 1 < label.size() && label[i + 1] == '_') {
                out.push_back('_');
                ++i;
            }
            continue;
        }
        out.push_back(label[i]);
    }
    return out;
}

uint32_t timestamp() {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

} // namespace

TrayMenu::TrayMenu(Shell& shell, sdbus::IConnection& bus, const std::string& service,
                   const std::string& path, wl_output* output, float anchor_x)
    : shell_(shell), anchor_x_(anchor_x) {
    proxy_ = sdbus::createProxy(bus, sdbus::ServiceName{service}, sdbus::ObjectPath{path});
    // The app may rebuild its menu while it is open.
    proxy_->uponSignal("LayoutUpdated")
        .onInterface(kMenuInterface)
        .call([this](uint32_t /*revision*/, int32_t /*parent*/) { fetch(); });

    window_ = std::make_unique<ui::LayerWindow>(
        shell_.app(), ui::LayerWindow::Options{
                          .layer = ui::LayerWindow::Layer::Overlay,
                          .anchor = ui::LayerWindow::kTop | ui::LayerWindow::kBottom |
                                    ui::LayerWindow::kLeft | ui::LayerWindow::kRight,
                          .width = 0,
                          .height = 0,
                          .exclusive_zone = -1, // whole output: a click outside closes
                          .keyboard = ui::LayerWindow::Keyboard::Exclusive,
                          .name_space = "tetra-tray-menu",
                          .output = output,
                      });
    window_->on_draw = [this] { draw(); };
    window_->on_closed = [this] { shell_.hide_tray_menu(); };

    // Some apps fill their menu only when told it is about to show.
    proxy_->callMethodAsync("AboutToShow")
        .onInterface(kMenuInterface)
        .withArguments(int32_t{0})
        .uponReplyInvoke([this](std::optional<sdbus::Error> /*error*/, bool /*update*/) { fetch(); });
}

TrayMenu::~TrayMenu() = default;

void TrayMenu::fetch() {
    proxy_->callMethodAsync("GetLayout")
        .onInterface(kMenuInterface)
        .withArguments(int32_t{0}, int32_t{-1}, std::vector<std::string>{})
        .uponReplyInvoke([this](std::optional<sdbus::Error> error, uint32_t /*revision*/,
                                Layout layout) {
            if (error) {
                std::println(stderr, "tetra-shell: tray menu: {}", error->what());
                failed_ = true;
            } else {
                root_ = parse(layout);
                loaded_ = true;
            }
            window_->request_redraw();
        });
}

TrayMenu::Node TrayMenu::parse(const Layout& layout) {
    Node node;
    node.id = layout.get<0>();
    const auto& props = layout.get<1>();
    auto string_prop = [&](const char* key) -> std::string {
        auto it = props.find(key);
        return it != props.end() && it->second.containsValueOfType<std::string>()
                   ? it->second.get<std::string>()
                   : std::string();
    };
    auto bool_prop = [&](const char* key, bool fallback) {
        auto it = props.find(key);
        return it != props.end() && it->second.containsValueOfType<bool>() ? it->second.get<bool>()
                                                                          : fallback;
    };
    node.label = strip_mnemonic(string_prop("label"));
    node.enabled = bool_prop("enabled", true);
    node.visible = bool_prop("visible", true);
    node.separator = string_prop("type") == "separator";
    const std::string toggle = string_prop("toggle-type");
    node.toggle = toggle == "checkmark" ? Node::Toggle::Check
                  : toggle == "radio"   ? Node::Toggle::Radio
                                        : Node::Toggle::None;
    if (auto it = props.find("toggle-state");
        it != props.end() && it->second.containsValueOfType<int32_t>()) {
        node.checked = it->second.get<int32_t>() == 1;
    }
    for (const auto& child : layout.get<2>()) {
        if (child.containsValueOfType<Layout>()) {
            node.children.push_back(parse(child.get<Layout>()));
        }
    }
    return node;
}

void TrayMenu::clicked(const Node& node) {
    proxy_->callMethodAsync("Event")
        .onInterface(kMenuInterface)
        .withArguments(node.id, std::string("clicked"), sdbus::Variant(int32_t{0}), timestamp())
        .uponReplyInvoke([](std::optional<sdbus::Error> /*error*/) {});
    shell_.hide_tray_menu();
}

void TrayMenu::draw_items(const std::vector<Node>& nodes) {
    for (const Node& node : nodes) {
        if (!node.visible) {
            continue;
        }
        if (node.separator) {
            ImGui::Separator();
            continue;
        }
        ImGui::PushID(node.id);
        if (!node.children.empty()) {
            if (ImGui::BeginMenu(node.label.c_str(), node.enabled)) {
                draw_items(node.children);
                ImGui::EndMenu();
            }
        } else if (ImGui::MenuItem(node.label.c_str(), nullptr,
                                   node.toggle != Node::Toggle::None && node.checked,
                                   node.enabled)) {
            clicked(node);
        }
        ImGui::PopID();
    }
}

void TrayMenu::draw() {
    const auto& p = shell_.app().palette();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    // Above the bar, centred on the icon, kept on screen.
    const float width = std::max(size_.x, kMinWidth);
    const float x = std::clamp(std::round(anchor_x_ - (width / 2)), 8.0F,
                               std::max(8.0F, viewport->Size.x - width - 8));
    const float bottom = viewport->Size.y - Bar::kHeight - 6;
    ImGui::SetNextWindowPos({x, bottom}, ImGuiCond_Always, {0, 1});
    ImGui::SetNextWindowSizeConstraints({kMinWidth, 0}, {viewport->Size.x - 16, viewport->Size.y});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10, 10});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {8, 9});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::to_imvec4(p.surface));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ui::to_imvec4(p.surface));
    // Until the first layout arrives, stay invisible (no flicker for fast apps).
    if (!loaded_ && !failed_) {
        ImGui::SetNextWindowBgAlpha(0);
    }
    ImGui::Begin("##tray-menu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
    if (failed_) {
        ImGui::BeginDisabled();
        ImGui::TextUnformatted("The app did not answer");
        ImGui::EndDisabled();
    } else if (loaded_) {
        if (root_.children.empty()) {
            ImGui::BeginDisabled();
            ImGui::TextUnformatted("No menu");
            ImGui::EndDisabled();
        }
        draw_items(root_.children);
    }
    const ImVec2 pos = ImGui::GetWindowPos();
    size_ = ImGui::GetWindowSize();
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
    if (++frames_ < 3) {
        window_->request_redraw(); // let the auto-size settle
    }

    // A click outside the panel (and outside any open submenu) closes it.
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool inside = mouse.x >= pos.x && mouse.x < pos.x + size_.x && mouse.y >= pos.y &&
                        mouse.y < pos.y + size_.y;
    const bool over_submenu = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) && !inside;
    if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !inside && !over_submenu) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        shell_.hide_tray_menu();
    }
}

} // namespace tetra::shell
