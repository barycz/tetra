#include "greeter.hpp"

#include "style.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <print>
#include <string_view>

#include <unistd.h>

namespace tetra::greeter {

namespace {

constexpr float kFormWidth = 320;
constexpr float kAvatarRadius = 44;

std::string format_time(const char* format) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    std::array<char, 128> buffer{};
    const std::size_t len = std::strftime(buffer.data(), buffer.size(), format, &local);
    return {buffer.data(), len};
}

void centered_text(std::string_view text, float width, float y, float size, ImU32 color) {
    ImGui::PushFont(nullptr, size);
    const ImVec2 extent = ImGui::CalcTextSize(text.data(), text.data() + text.size());
    ImGui::GetWindowDrawList()->AddText({std::round((width - extent.x) / 2), y}, color, text.data(),
                                        text.data() + text.size());
    ImGui::PopFont();
}

// The first character of a name, upper-cased if it is ASCII.
std::string initial(std::string_view name) {
    if (name.empty()) {
        return "?";
    }
    const auto first = static_cast<unsigned char>(name.front());
    if (first < 0x80) {
        return {static_cast<char>(std::toupper(first))};
    }
    std::size_t len = 1;
    while (len < name.size() && (static_cast<unsigned char>(name[len]) & 0xC0) == 0x80) {
        ++len;
    }
    return std::string(name.substr(0, len));
}

const char* env(const char* name) {
    return std::getenv(name); // NOLINT(concurrency-mt-unsafe)
}

} // namespace

Greeter::Greeter(ui::App& app) : app_(app), greetd_(app), state_path_(state_file()) {
    // TETRA_GREETER_PASSWD: another passwd file (the tests' made-up users).
    const char* passwd = env("TETRA_GREETER_PASSWD");
    users_ = read_users(passwd != nullptr ? passwd : "/etc/passwd", read_uid_range("/etc/login.defs"));
    sessions_ = session_entries(session_dirs());
    state_ = load_state(state_path_);
    if (auto it = std::ranges::find(users_, state_.user, &User::name); it != users_.end()) {
        user_ = static_cast<std::size_t>(it - users_.begin());
    } else if (!state_.user.empty() || users_.empty()) {
        // Remembered but not listed (a system account, LDAP): type it.
        user_ = users_.size();
        state_.user.copy(typed_name_.data(), typed_name_.size() - 1);
        focus_name_ = state_.user.empty();
    }
    // The last session, else tetra's own, else the first.
    auto by_id = [&](std::string_view id) { return std::ranges::find(sessions_, id, &DesktopEntry::id); };
    if (auto it = by_id(state_.session); it != sessions_.end()) {
        session_ = static_cast<std::size_t>(it - sessions_.begin());
    } else if (auto tetra = by_id("tetra"); tetra != sessions_.end()) {
        session_ = static_cast<std::size_t>(tetra - sessions_.begin());
    }

    greetd_.on_reply = [this](const Reply& reply) { handle_reply(reply); };
    greetd_.on_disconnected = [this] {
        stage_ = Stage::Idle;
        error_ = "Lost the connection to greetd";
        redraw();
    };
    if (const char* socket = env("GREETD_SOCK"); socket != nullptr && *socket != '\0') {
        if (auto connected = greetd_.connect(socket); !connected) {
            error_ = "Cannot reach greetd: " + connected.error();
        }
    } else {
        error_ = "Not started by greetd (GREETD_SOCK is not set)";
    }
    if (sessions_.empty()) {
        error_ = "No sessions installed (wayland-sessions)";
    }

    system_bus_ = ui::DBusConnection::system(app_);
    power_ = std::make_unique<ui::Power>(system_bus_ ? &system_bus_->connection() : nullptr);
    power_->on_changed = [this] { redraw(); };
    power_->refresh();

    app_.on_output_added = [this](const ui::OutputInfo& output) { add_output(output); };
    app_.on_output_removed = [this](const ui::OutputInfo& output) { windows_.erase(output.global); };
    for (const ui::OutputInfo* output : app_.outputs()) {
        add_output(*output);
    }
    app_.on_config_changed = [this] { redraw(); };
    schedule_clock();
}

Greeter::~Greeter() {
    app_.on_output_added = nullptr;
    app_.on_output_removed = nullptr;
    app_.on_config_changed = nullptr;
    if (clock_timer_ != 0) {
        app_.cancel_timer(clock_timer_);
    }
    windows_.clear();
    wipe();
}

void Greeter::add_output(const ui::OutputInfo& output) {
    if (windows_.contains(output.global)) {
        return;
    }
    auto window = std::make_unique<ui::LayerWindow>(
        app_, ui::LayerWindow::Options{
                  .layer = ui::LayerWindow::Layer::Overlay,
                  .anchor = ui::LayerWindow::kTop | ui::LayerWindow::kBottom | ui::LayerWindow::kLeft |
                            ui::LayerWindow::kRight,
                  .exclusive_zone = -1,
                  .keyboard = ui::LayerWindow::Keyboard::Exclusive,
                  .name_space = "tetra-greeter",
                  .output = output.output,
              });
    window->on_draw = [this] { draw(); };
    windows_[output.global] = std::move(window);
}

void Greeter::redraw() {
    for (auto& [global, window] : windows_) {
        window->request_redraw();
    }
}

void Greeter::schedule_clock() {
    // Wake at the next full minute.
    const auto now = std::chrono::system_clock::now();
    const auto next = std::chrono::ceil<std::chrono::minutes>(now + std::chrono::milliseconds(1));
    clock_timer_ = app_.add_timer(std::chrono::duration_cast<std::chrono::milliseconds>(next - now), [this] {
        clock_timer_ = 0;
        redraw();
        schedule_clock();
    });
}

std::string Greeter::username() const {
    return user_ < users_.size() ? users_[user_].name : std::string(typed_name_.data());
}

void Greeter::wipe() {
    explicit_bzero(answer_.data(), answer_.size());
    explicit_bzero(pending_password_.data(), pending_password_.size());
    pending_password_.clear();
    password_pending_ = false;
}

void Greeter::submit() {
    if (!greetd_.connected() || sessions_.empty()) {
        return;
    }
    if (stage_ == Stage::Prompt) {
        std::string answer(answer_.data());
        explicit_bzero(answer_.data(), answer_.size());
        stage_ = Stage::Authenticating;
        greetd_.send(post_auth_message_response(answer));
        explicit_bzero(answer.data(), answer.size());
        redraw();
        return;
    }
    if (stage_ != Stage::Idle) {
        return;
    }
    const std::string name = username();
    if (name.empty()) {
        error_ = "Enter a user name";
        focus_name_ = true;
        redraw();
        return;
    }
    // greetd asks for the password next (usually): answer it with what
    // was typed instead of asking again.
    pending_password_ = answer_.data();
    password_pending_ = true;
    explicit_bzero(answer_.data(), answer_.size());
    error_.clear();
    info_.clear();
    stage_ = Stage::Authenticating;
    greetd_.send(create_session(name));
    redraw();
}

void Greeter::handle_reply(const Reply& reply) {
    redraw();
    if (stage_ == Stage::Cancelling) {
        stage_ = Stage::Idle; // the answer to cancel_session
        return;
    }
    switch (reply.type) {
    case Reply::Type::AuthMessage:
        if (reply.prompt == Reply::Prompt::Info || reply.prompt == Reply::Prompt::Error) {
            (reply.prompt == Reply::Prompt::Error ? error_ : info_) = reply.message;
            greetd_.send(post_auth_message_response(std::nullopt));
            return;
        }
        if (reply.prompt == Reply::Prompt::Secret && password_pending_) {
            std::string password = std::move(pending_password_);
            pending_password_.clear();
            password_pending_ = false;
            greetd_.send(post_auth_message_response(password));
            explicit_bzero(password.data(), password.size());
            return;
        }
        stage_ = Stage::Prompt;
        prompt_ = reply.message;
        prompt_secret_ = reply.prompt == Reply::Prompt::Secret;
        explicit_bzero(answer_.data(), answer_.size());
        return;
    case Reply::Type::Success:
        if (stage_ == Stage::Starting) {
            // greetd runs the session once we are gone.
            state_.user = username();
            state_.session = sessions_[session_].id;
            save_state(state_path_, state_);
            wipe();
            app_.quit(0);
            return;
        }
        start_session();
        return;
    case Reply::Type::Error:
        fail(reply.error_type == "auth_error" ? (prompt_.empty() ? "Wrong password" : "Authentication failed")
                                              : reply.description);
        return;
    case Reply::Type::Invalid:
        return;
    }
}

void Greeter::start_session() {
    const DesktopEntry& session = sessions_[session_];
    const auto cmd = exec_argv(session, "");
    std::vector<std::string> env{"XDG_SESSION_TYPE=wayland", "XDG_SESSION_DESKTOP=" + session.id};
    if (!session.desktop_names.empty()) {
        std::string names;
        for (const auto& name : session.desktop_names) {
            names += (names.empty() ? "" : ":") + name;
        }
        env.push_back("XDG_CURRENT_DESKTOP=" + names);
    }
    stage_ = Stage::Starting;
    greetd_.send(tetra::greeter::start_session(cmd, env));
}

void Greeter::fail(std::string message) {
    wipe();
    error_ = std::move(message);
    prompt_.clear();
    stage_ = Stage::Cancelling;
    greetd_.send(cancel_session());
}

void Greeter::draw() {
    const auto& p = app_.palette();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float width = viewport->Size.x;
    const float height = viewport->Size.y;

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::to_imvec4(app_.config().desktop.background));
    ImGui::Begin("##greeter", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    // Only the full-screen window has no padding and corners: not the lists.
    ImGui::PopStyleVar(3);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Clock and date at the top.
    float y = std::round(height * 0.12F);
    centered_text(format_time("%H:%M"), width, y, 72, ui::to_imu32(p.text));
    y += 84;
    centered_text(format_time("%A, %e %B"), width, y, 18, ui::to_imu32(p.text_dim));

    // The card: avatar, user, password, status.
    const float card_w = kFormWidth + 64;
    const bool typed = user_ >= users_.size();
    const float card_h = 300 + (typed && !users_.empty() ? 38 : 0); // a user name field too
    const float card_x = std::round((width - card_w) / 2);
    const float card_y = std::round(std::max(y + 48, (height - card_h) / 2 + 30));
    draw->AddRectFilled({card_x, card_y}, {card_x + card_w, card_y + card_h}, ui::to_imu32(p.surface), 14);
    draw->AddRect({card_x, card_y}, {card_x + card_w, card_y + card_h}, ui::to_imu32(p.separator), 14);

    const float cx = width / 2;
    float fy = card_y + 28;
    const ImVec2 centre{cx, fy + kAvatarRadius};
    draw->AddCircleFilled(centre, kAvatarRadius, ui::to_imu32(p.accent), 48);
    const std::string name = user_ < users_.size() ? users_[user_].display_name : std::string(typed_name_.data());
    {
        ImGui::PushFont(nullptr, 40);
        const std::string letter = initial(name);
        const ImVec2 extent = ImGui::CalcTextSize(letter.c_str());
        draw->AddText({std::round(cx - extent.x / 2), std::round(centre.y - extent.y / 2)},
                      ui::to_imu32(p.accent_text), letter.c_str());
        ImGui::PopFont();
    }
    fy += kAvatarRadius * 2 + 20;

    const bool busy = stage_ == Stage::Authenticating || stage_ == Stage::Starting || stage_ == Stage::Cancelling;
    const float field_x = std::round((width - kFormWidth) / 2);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {12, 9});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ui::to_imvec4(p.window));
    ImGui::BeginDisabled(busy || stage_ == Stage::Prompt);
    ImGui::SetCursorPos({field_x, fy});
    draw_user_picker(field_x, kFormWidth);
    ImGui::EndDisabled();
    fy = ImGui::GetCursorPosY() + 10;

    // The password, or PAM's further question.
    ImGui::BeginDisabled(busy);
    ImGui::SetCursorPos({field_x, fy});
    const bool popup_open = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    if (!busy && !popup_open && !ImGui::IsAnyItemActive() && !focus_name_) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(kFormWidth);
    const char* hint = stage_ == Stage::Prompt ? prompt_.c_str() : "Password";
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue;
    if (stage_ != Stage::Prompt || prompt_secret_) {
        flags |= ImGuiInputTextFlags_Password;
    }
    const bool entered = ImGui::InputTextWithHint("##answer", hint, answer_.data(), answer_.size(), flags);
    if (ImGui::IsItemEdited() && !error_.empty()) {
        error_.clear();
    }
    const bool escape = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape);
    fy += ImGui::GetFrameHeight() + 10;

    // A button for mouse users.
    ImGui::SetCursorPos({field_x, fy});
    ImGui::PushStyleColor(ImGuiCol_Button, ui::to_imvec4(p.accent));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::to_imvec4(ui::mix(p.accent, p.hover)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui::to_imvec4(p.accent));
    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.accent_text));
    const bool clicked = ImGui::Button(stage_ == Stage::Prompt ? "Continue" : "Log in", {kFormWidth, 0});
    ImGui::PopStyleColor(4);
    ImGui::EndDisabled();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    fy += ImGui::GetFrameHeight() + 14;

    if (entered || clicked) {
        ImGui::ClearActiveID(); // drop the field's copy of the text
        submit();
    } else if (escape && stage_ == Stage::Prompt) {
        fail("");
    }

    if (stage_ == Stage::Authenticating || stage_ == Stage::Cancelling) {
        centered_text("Checking…", width, fy, 15, ui::to_imu32(p.text_dim));
    } else if (stage_ == Stage::Starting) {
        centered_text("Starting " + sessions_[session_].name + "…", width, fy, 15, ui::to_imu32(p.text_dim));
    } else if (!error_.empty()) {
        centered_text(error_, width, fy, 15, ui::to_imu32(p.close_hover));
    } else if (!info_.empty()) {
        centered_text(info_, width, fy, 15, ui::to_imu32(p.text_dim));
    }

    draw_footer(width, height);

    ImGui::End();
    ImGui::PopStyleColor();
}

void Greeter::draw_user_picker(float x, float width) {
    const auto& p = app_.palette();
    const bool other = user_ >= users_.size();
    if (!users_.empty()) {
        ImGui::SetNextItemWidth(width);
        const std::string preview = other ? std::string("Other user…") : users_[user_].display_name;
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ui::to_imvec4(p.surface));
        if (ImGui::BeginCombo("##user", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
            for (std::size_t i = 0; i <= users_.size(); ++i) {
                const bool is_other = i == users_.size();
                std::string label = is_other ? std::string("Other user…")
                                             : users_[i].display_name +
                                                   (users_[i].display_name != users_[i].name
                                                        ? "  (" + users_[i].name + ")"
                                                        : std::string());
                label += "##" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), i == user_)) {
                    user_ = i;
                    error_.clear();
                    focus_name_ = is_other;
                    redraw();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor();
    }
    if (user_ >= users_.size()) { // (just) chosen in the list
        ImGui::SetCursorPosX(x); // below the list
        if (focus_name_ && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(width);
        if (ImGui::InputTextWithHint("##name", "User name", typed_name_.data(), typed_name_.size(),
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
            ImGui::ClearActiveID(); // on to the password field
        }
        if (ImGui::IsItemActive()) {
            focus_name_ = false; // until then the password field keeps its hands off
        }
    } else {
        focus_name_ = false;
    }
}

void Greeter::draw_footer(float width, float height) {
    const auto& p = app_.palette();
    const float margin = 24;
    const float y = height - margin - ImGui::GetFrameHeight();
    const bool busy = stage_ != Stage::Idle;

    // Session, bottom left.
    if (!sessions_.empty()) {
        ImGui::SetCursorPos({margin, y});
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ui::to_imvec4(p.text_dim), "Session");
        ImGui::SameLine();
        ImGui::BeginDisabled(busy);
        ImGui::SetNextItemWidth(200);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ui::to_imvec4(p.surface));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ui::to_imvec4(p.surface));
        if (ImGui::BeginCombo("##session", sessions_[session_].name.c_str())) {
            for (std::size_t i = 0; i < sessions_.size(); ++i) {
                const std::string label = sessions_[i].name + "##" + sessions_[i].id;
                if (ImGui::Selectable(label.c_str(), i == session_)) {
                    session_ = i;
                }
                if (ImGui::IsItemHovered() && !sessions_[i].comment.empty()) {
                    ImGui::SetTooltip("%s", sessions_[i].comment.c_str());
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(2);
        ImGui::EndDisabled();
    }

    // Power, bottom right.
    using Action = ui::Power::Action;
    float x = width - margin;
    const ImGuiStyle& style = ImGui::GetStyle();
    for (const Action action : {Action::PowerOff, Action::Reboot, Action::Suspend}) {
        if (!power_->can(action)) {
            continue;
        }
        const char* label = ui::Power::label(action);
        x -= ImGui::CalcTextSize(label).x + (style.FramePadding.x * 2);
        ImGui::SetCursorPos({x, y});
        ImGui::PushStyleColor(ImGuiCol_Button, ui::to_imvec4(p.surface));
        if (ImGui::Button(label)) {
            power_->request(action);
        }
        ImGui::PopStyleColor();
        x -= 8;
    }
}

} // namespace tetra::greeter
