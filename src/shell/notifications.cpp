#include "notifications.hpp"

#include "shell.hpp"
#include "style.hpp"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <print>

namespace tetra::shell {

namespace {

const char* const kInterface = "org.freedesktop.Notifications";
const char* const kPath = "/org/freedesktop/Notifications";

constexpr int32_t kDefaultTimeoutMs = 6000;
constexpr std::size_t kMaxVisible = 4;
constexpr float kWidth = 380;
constexpr float kMargin = 12; // from the screen edge and the bar
constexpr float kPad = 12;
constexpr float kIconSize = 36;
constexpr float kGap = 8; // between cards

using ImageData = sdbus::Struct<int32_t, int32_t, int32_t, bool, int32_t, int32_t, std::vector<uint8_t>>;

std::string strip_file_uri(std::string value) {
    if (value.starts_with("file://")) {
        value.erase(0, 7);
    }
    return value;
}

} // namespace

// --- D-Bus service -------------------------------------------------------------------

Notifications::Notifications(Shell& shell, sdbus::IConnection& connection)
    : shell_(shell), connection_(connection),
      object_(sdbus::createObject(connection, sdbus::ObjectPath{kPath})),
      popups_(std::make_unique<NotificationPopups>(*this)) {
    object_
        ->addVTable(
            sdbus::registerMethod("Notify")
                .withInputParamNames("app_name", "replaces_id", "app_icon", "summary", "body",
                                     "actions", "hints", "expire_timeout")
                .withOutputParamNames("id")
                .implementedAs([this](const std::string& app_name, uint32_t replaces_id,
                                      const std::string& app_icon, const std::string& summary,
                                      const std::string& body, const std::vector<std::string>& actions,
                                      const std::map<std::string, sdbus::Variant>& hints,
                                      int32_t expire_timeout) {
                    return notify(app_name, replaces_id, app_icon, summary, body, actions, hints,
                                  expire_timeout);
                }),
            sdbus::registerMethod("CloseNotification")
                .withInputParamNames("id")
                .implementedAs([this](uint32_t id) { close(id, CloseReason::Closed); }),
            sdbus::registerMethod("GetCapabilities")
                .withOutputParamNames("capabilities")
                .implementedAs([] {
                    return std::vector<std::string>{"body", "actions", "icon-static"};
                }),
            sdbus::registerMethod("GetServerInformation")
                .withOutputParamNames("name", "vendor", "version", "spec_version")
                .implementedAs([] {
                    return std::make_tuple(std::string("tetra-shell"), std::string("tetra"),
                                           std::string("0.1"), std::string("1.2"));
                }),
            sdbus::registerSignal("NotificationClosed").withParameters<uint32_t, uint32_t>("id", "reason"),
            sdbus::registerSignal("ActionInvoked").withParameters<uint32_t, std::string>("id", "action_key"))
        .forInterface(sdbus::InterfaceName{kInterface});
    connection_.requestName(sdbus::ServiceName{kInterface});
}

Notifications::~Notifications() {
    for (const auto& notification : list_) {
        if (notification->timer) {
            shell_.app().cancel_timer(*notification->timer);
        }
    }
    try {
        connection_.releaseName(sdbus::ServiceName{kInterface});
    } catch (const sdbus::Error&) {
        // the bus may already be gone at shutdown
    }
}

Notification* Notifications::find(uint32_t id) {
    auto it = std::ranges::find_if(list_, [&](const auto& n) { return n->id == id; });
    return it == list_.end() ? nullptr : it->get();
}

uint32_t Notifications::notify(const std::string& app_name, uint32_t replaces_id,
                               const std::string& app_icon, const std::string& summary,
                               const std::string& body, const std::vector<std::string>& actions,
                               const std::map<std::string, sdbus::Variant>& hints,
                               int32_t expire_timeout) {
    auto hint = [&](const char* key) -> const sdbus::Variant* {
        auto it = hints.find(key);
        return it == hints.end() ? nullptr : &it->second;
    };
    auto string_hint = [&](const char* key) -> std::string {
        const sdbus::Variant* v = hint(key);
        return v != nullptr && v->containsValueOfType<std::string>() ? v->get<std::string>() : std::string{};
    };
    auto bool_hint = [&](const char* key) {
        const sdbus::Variant* v = hint(key);
        return v != nullptr && v->containsValueOfType<bool>() && v->get<bool>();
    };

    auto notification = std::make_unique<Notification>();
    Notification& n = *notification;
    n.app_name = app_name;
    n.summary = summary;
    n.body = body;
    n.resident = bool_hint("resident");
    if (const sdbus::Variant* v = hint("urgency"); v != nullptr && v->containsValueOfType<uint8_t>()) {
        n.urgency = std::min<uint8_t>(v->get<uint8_t>(), 2);
    }
    for (std::size_t i = 0; i + 1 < actions.size(); i += 2) {
        n.actions.emplace_back(actions[i], actions[i + 1]);
    }

    // Image: raw pixels, then an image path, then the app icon, then the
    // icon of the app's desktop entry.
    for (const char* key : {"image-data", "image_data", "icon_data"}) {
        if (const sdbus::Variant* v = hint(key); v != nullptr && v->containsValueOfType<ImageData>()) {
            const auto data = v->get<ImageData>();
            n.image = Notification::Image{.width = std::get<0>(data),
                                          .height = std::get<1>(data),
                                          .stride = std::get<2>(data),
                                          .channels = std::get<5>(data),
                                          .pixels = std::get<6>(data)};
            break;
        }
    }
    std::string image_path = string_hint("image-path");
    if (image_path.empty()) {
        image_path = string_hint("image_path");
    }
    const std::string desktop_entry = string_hint("desktop-entry");
    const DesktopEntry* entry = shell_.applications().find(desktop_entry);
    if (entry == nullptr) {
        entry = shell_.applications().match_app_id(app_name);
    }
    if (!image_path.empty()) {
        n.icon = strip_file_uri(image_path);
    } else if (!app_icon.empty()) {
        n.icon = strip_file_uri(app_icon);
    } else if (entry != nullptr) {
        n.icon = entry->icon;
    }
    if (n.app_name.empty() && entry != nullptr) {
        n.app_name = entry->name;
    }

    // Critical notifications stay until dismissed; -1 means "server default".
    if (n.urgency == 2 || expire_timeout == 0) {
        n.timeout_ms = 0;
    } else {
        n.timeout_ms = expire_timeout < 0 ? kDefaultTimeoutMs : expire_timeout;
    }

    if (Notification* old = replaces_id != 0 ? find(replaces_id) : nullptr) {
        n.id = replaces_id;
        if (old->timer) {
            shell_.app().cancel_timer(*old->timer);
        }
        *std::ranges::find_if(list_, [&](const auto& p) { return p.get() == old; }) = std::move(notification);
    } else {
        n.id = next_id_++;
        if (next_id_ == 0) {
            next_id_ = 1; // 0 is reserved
        }
        list_.push_back(std::move(notification));
    }
    arm_timer(n);
    popups_->update();
    return n.id;
}

void Notifications::arm_timer(Notification& n) {
    if (n.timeout_ms <= 0) {
        return;
    }
    const uint32_t id = n.id;
    n.timer = shell_.app().add_timer(std::chrono::milliseconds(n.timeout_ms), [this, id] {
        Notification* current = find(id);
        if (current == nullptr) {
            return;
        }
        current->timer.reset();
        if (current->hovered) {
            // Never expire under the pointer; check again shortly.
            current->timer = shell_.app().add_timer(std::chrono::seconds(1), [this, id] {
                if (Notification* again = find(id)) {
                    again->timer.reset();
                    if (again->hovered) {
                        arm_timer(*again);
                    } else {
                        close(id, CloseReason::Expired);
                    }
                }
            });
            return;
        }
        close(id, CloseReason::Expired);
    });
}

void Notifications::close(uint32_t id, CloseReason reason) {
    auto it = std::ranges::find_if(list_, [&](const auto& n) { return n->id == id; });
    if (it == list_.end()) {
        return;
    }
    if ((*it)->timer) {
        shell_.app().cancel_timer(*(*it)->timer);
    }
    list_.erase(it);
    object_->emitSignal("NotificationClosed")
        .onInterface(sdbus::InterfaceName{kInterface})
        .withArguments(id, static_cast<uint32_t>(reason));
    popups_->update();
}

void Notifications::dismiss(uint32_t id) {
    close(id, CloseReason::Dismissed);
}

void Notifications::post(const std::string& summary, const std::string& body,
                         const std::string& image_path, std::function<void()> on_activate) {
    std::map<std::string, sdbus::Variant> hints;
    if (!image_path.empty()) {
        hints.emplace("image-path", sdbus::Variant(image_path));
    }
    const uint32_t id = notify("tetra", 0, "", summary, body, {"default", "Open"}, hints, -1);
    if (Notification* n = find(id)) {
        n->on_activate = std::move(on_activate);
    }
}

void Notifications::invoke(uint32_t id, const std::string& action) {
    Notification* n = find(id);
    if (n == nullptr) {
        return;
    }
    if (n->on_activate) {
        auto activate = n->on_activate; // close() frees *n
        close(id, CloseReason::Dismissed);
        activate();
        return;
    }
    const bool resident = n->resident;
    object_->emitSignal("ActionInvoked")
        .onInterface(sdbus::InterfaceName{kInterface})
        .withArguments(id, action);
    if (!resident) {
        close(id, CloseReason::Dismissed);
    }
}

void Notifications::set_hovered(uint32_t id, bool hovered) {
    if (Notification* n = find(id)) {
        n->hovered = hovered;
    }
}

// --- popups ------------------------------------------------------------------------------

NotificationPopups::NotificationPopups(Notifications& notifications) : notifications_(notifications) {}

void NotificationPopups::update() {
    ui::App& app = notifications_.shell().app();
    if (notifications_.list().empty()) {
        if (window_) {
            // Possibly called from our own draw callback: destroy it later.
            std::shared_ptr<ui::LayerWindow> closing = std::move(window_);
            app.add_timer(std::chrono::milliseconds(0), [closing] {});
        }
        return;
    }
    if (!window_) {
        ui::LayerWindow::Options options{
            .layer = ui::LayerWindow::Layer::Top,
            .anchor = ui::LayerWindow::kBottom | ui::LayerWindow::kRight,
            .width = static_cast<int>(kWidth),
            .height = 120, // corrected after the first frame
            .exclusive_zone = 0,
            .keyboard = ui::LayerWindow::Keyboard::None,
            .name_space = "tetra-notifications",
            .output = nullptr,
        };
        options.margin.bottom = static_cast<int>(kMargin);
        options.margin.right = static_cast<int>(kMargin);
        window_ = std::make_shared<ui::LayerWindow>(app, options);
        window_->on_draw = [this] { draw(); };
    }
    window_->request_redraw();
}

void NotificationPopups::draw() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin("##notifications", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);

    // Newest at the bottom, next to the bar. Copy the ids: acting on a card
    // changes the list.
    const auto& list = notifications_.list();
    std::vector<uint32_t> ids;
    for (std::size_t i = list.size() > kMaxVisible ? list.size() - kMaxVisible : 0; i < list.size(); ++i) {
        ids.push_back(list[i]->id);
    }
    float y = 0;
    for (const uint32_t id : ids) {
        auto it = std::ranges::find_if(notifications_.list(), [&](const auto& n) { return n->id == id; });
        if (it != notifications_.list().end()) {
            y = draw_card(**it, y, kWidth) + kGap;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();

    const int height = std::max(1, static_cast<int>(std::ceil(y - kGap)));
    if (window_ && height != window_->requested_height()) {
        window_->set_size(static_cast<int>(kWidth), height);
    }
}

float NotificationPopups::draw_card(Notification& n, float y, float width) {
    Shell& shell = notifications_.shell();
    const auto& p = shell.app().palette();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float font_size = ImGui::GetStyle().FontSizeBase;
    const float scale = ImGui::GetIO().DisplayFramebufferScale.x;

    // Content first (channel 1), then the background behind it (channel 0)
    // once the height is known.
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    ImGui::PushID(static_cast<int>(n.id));

    std::optional<std::pair<uint32_t, std::string>> pending_action;
    bool pending_dismiss = false;
    const float top = y + kPad;

    // Icon: raw image data, else an icon name or file.
    ImTextureRef texture;
    bool has_icon = false;
    ImU32 tint = IM_COL32_WHITE;
    if (n.image && !n.texture) {
        n.texture = std::make_unique<ui::Texture>(n.image->width, n.image->height, n.image->stride,
                                                  n.image->channels, n.image->pixels);
    }
    if (n.texture && n.texture->valid()) {
        texture = n.texture->ref();
        has_icon = true;
    } else if (const ui::Icon* icon = shell.icons().get(n.icon, static_cast<int>(kIconSize * scale))) {
        texture = icon->ref();
        has_icon = true;
        if (icon->symbolic) {
            tint = ui::to_imu32(n.urgency == 2 ? p.close_hover : p.text);
        }
    }
    if (has_icon) {
        dl->AddImage(texture, {kPad, top}, {kPad + kIconSize, top + kIconSize}, {0, 0}, {1, 1}, tint);
    }
    const float text_x = has_icon ? kPad + kIconSize + 12 : kPad;

    // App name, small and quiet, with the close button on the right.
    ImGui::PushFont(nullptr, std::round(font_size * 0.86F));
    ImGui::SetCursorPos({text_x, top});
    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.text_dim));
    ImGui::TextUnformatted(n.app_name.empty() ? "Notification" : n.app_name.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();

    const ImVec2 close_min{width - kPad - 18, top - 2};
    ImGui::SetCursorPos(close_min);
    if (ImGui::InvisibleButton("##close", {18, 18})) {
        pending_dismiss = true;
    }
    const ImU32 close_color = ui::to_imu32(ImGui::IsItemHovered() ? p.text : p.text_dim);
    const ImVec2 c{close_min.x + 9, close_min.y + 9};
    dl->AddLine({c.x - 4, c.y - 4}, {c.x + 4, c.y + 4}, close_color, 1.5F);
    dl->AddLine({c.x + 4, c.y - 4}, {c.x - 4, c.y + 4}, close_color, 1.5F);

    // Summary and body, wrapped to the card.
    ImGui::PushTextWrapPos(width - kPad);
    ImGui::SetCursorPos({text_x, ImGui::GetCursorPosY() + 2});
    ImGui::PushFont(nullptr, std::round(font_size * 1.07F));
    ImGui::TextUnformatted(n.summary.c_str());
    ImGui::PopFont();
    if (!n.body.empty()) {
        ImGui::SetCursorPosX(text_x);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.text.with_alpha(0.8F)));
        ImGui::TextUnformatted(n.body.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::PopTextWrapPos();

    // Action buttons ("default" is the card itself).
    bool first_button = true;
    for (const auto& [key, label] : n.actions) {
        if (key == "default") {
            continue;
        }
        if (first_button) {
            ImGui::SetCursorPos({text_x, ImGui::GetCursorPosY() + 4});
            first_button = false;
        } else {
            ImGui::SameLine();
        }
        if (ImGui::Button(label.c_str())) {
            pending_action = std::pair{n.id, key};
        }
    }

    const float bottom = std::max(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y,
                                  top + kIconSize) + kPad;
    ImGui::PopID();

    // Card background and border; critical ones get a red border.
    dl->ChannelsSetCurrent(0);
    const ImVec2 min{0, y};
    const ImVec2 max{width, bottom};
    dl->AddRectFilled(min, max, ui::to_imu32(p.surface), 10);
    dl->AddRect(min, max, ui::to_imu32(n.urgency == 2 ? p.close_hover : p.separator), 10);
    dl->ChannelsMerge();

    // Hover pauses the timeout; a click elsewhere on the card runs the
    // default action, or dismisses it.
    const bool hovered = ImGui::IsMouseHoveringRect(min, max, false);
    const uint32_t id = n.id;
    notifications_.set_hovered(id, hovered);
    if (hovered && !pending_dismiss && !pending_action && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsAnyItemHovered()) {
        const bool has_default = std::ranges::any_of(n.actions, [](const auto& a) { return a.first == "default"; });
        if (has_default) {
            pending_action = std::pair{id, std::string("default")};
        } else {
            pending_dismiss = true;
        }
    }

    // Act last: these may remove `n` from the list.
    if (pending_action) {
        notifications_.invoke(pending_action->first, pending_action->second);
    } else if (pending_dismiss) {
        notifications_.dismiss(id);
    }
    return bottom;
}

} // namespace tetra::shell
