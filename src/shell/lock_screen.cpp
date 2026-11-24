#include "lock_screen.hpp"

#include "protocols.hpp"
#include "shell.hpp"
#include "style.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <cmath>
#include <ctime>
#include <string_view>

#include <pwd.h>
#include <unistd.h>

namespace tetra::shell {

namespace {

constexpr float kFieldWidth = 300;

std::string format_time(const char* format) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    std::array<char, 128> buffer{};
    const std::size_t len = std::strftime(buffer.data(), buffer.size(), format, &local);
    return {buffer.data(), len};
}

std::string user_display_name() {
    const passwd* pw = getpwuid(getuid()); // NOLINT(concurrency-mt-unsafe): UI thread only
    if (pw == nullptr) {
        return {};
    }
    // GECOS: "Full Name,room,phone,..."
    const std::string_view gecos = pw->pw_gecos != nullptr ? pw->pw_gecos : "";
    const std::string_view full = gecos.substr(0, gecos.find(','));
    return full.empty() ? std::string(pw->pw_name) : std::string(full);
}

void centered_text(const std::string& text, float width, float y, float size, ImU32 color) {
    ImGui::PushFont(nullptr, size);
    const ImVec2 extent = ImGui::CalcTextSize(text.c_str());
    ImGui::GetWindowDrawList()->AddText({std::round((width - extent.x) / 2), y}, color,
                                        text.c_str());
    ImGui::PopFont();
}

} // namespace

struct LockScreenListener {
    static const ext_session_lock_v1_listener listener;
};

const ext_session_lock_v1_listener LockScreenListener::listener = {
    .locked =
        [](void* data, ext_session_lock_v1* /*lock*/) {
            auto* self = static_cast<LockScreen*>(data);
            self->locked_ = true;
            if (self->on_changed) {
                self->on_changed(true);
            }
            if (self->unlock_pending_) {
                self->unlock();
            }
        },
    .finished =
        [](void* data, ext_session_lock_v1* /*lock*/) {
            // Refused (another lock client holds the session) or ended by
            // the compositor.
            static_cast<LockScreen*>(data)->finish();
        },
};

LockScreen::LockScreen(Shell& shell, ext_session_lock_manager_v1* manager)
    : shell_(shell), manager_(manager), auth_(shell.app()), user_name_(user_display_name()) {}

LockScreen::~LockScreen() {
    windows_.clear();
    // Never "destroy" a lock that was confirmed: that is a protocol error.
    // If the shell exits while locked, the session simply stays locked.
    if (lock_ != nullptr) {
        if (locked_) {
            // Free our side only: no request goes to the compositor.
            wl_proxy_destroy(reinterpret_cast<wl_proxy*>(lock_)); // NOLINT
        } else {
            ext_session_lock_v1_destroy(lock_);
        }
    }
    if (manager_ != nullptr) {
        ext_session_lock_manager_v1_destroy(manager_);
    }
    explicit_bzero(password_.data(), password_.size());
}

void LockScreen::lock(const std::map<uint32_t, ui::OutputInfo>& outputs) {
    if (lock_ != nullptr || manager_ == nullptr) {
        return;
    }
    lock_ = ext_session_lock_manager_v1_lock(manager_);
    ext_session_lock_v1_add_listener(lock_, &LockScreenListener::listener, this);
    error_.clear();
    unlock_pending_ = false;
    for (const auto& [global, output] : outputs) {
        add_output(output);
    }
}

void LockScreen::add_output(const ui::OutputInfo& output) {
    if (lock_ == nullptr || windows_.contains(output.global)) {
        return;
    }
    auto window = std::make_unique<ui::LockWindow>(shell_.app(), lock_, output.output);
    window->on_draw = [this] { draw(); };
    windows_[output.global] = std::move(window);
}

void LockScreen::remove_output(const ui::OutputInfo& output) {
    windows_.erase(output.global);
}

void LockScreen::redraw() {
    for (auto& [global, window] : windows_) {
        window->request_redraw();
    }
}

void LockScreen::clear_password() {
    explicit_bzero(password_.data(), password_.size());
}

void LockScreen::submit() {
    if (auth_.busy() || password_.front() == '\0') {
        return;
    }
    std::string password(password_.data());
    clear_password();
    error_.clear();
    auth_.start(std::move(password), [this](bool ok) {
        if (ok) {
            unlock();
        } else {
            error_ = "Wrong password";
            redraw();
        }
    });
    redraw();
}

void LockScreen::unlock() {
    if (lock_ == nullptr) {
        return;
    }
    if (!locked_) {
        unlock_pending_ = true; // allowed only once the compositor said "locked"
        return;
    }
    ext_session_lock_v1_unlock_and_destroy(lock_);
    lock_ = nullptr;
    locked_ = false;
    unlock_pending_ = false;
    windows_.clear();
    wl_display_flush(shell_.app().display());
    if (on_changed) {
        on_changed(false);
    }
}

void LockScreen::finish() {
    if (lock_ != nullptr) {
        ext_session_lock_v1_destroy(lock_);
        lock_ = nullptr;
    }
    const bool was_locked = locked_;
    locked_ = false;
    unlock_pending_ = false;
    windows_.clear();
    if (was_locked && on_changed) {
        on_changed(false);
    }
}

void LockScreen::draw() {
    const auto& p = shell_.app().palette();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float width = viewport->Size.x;
    const float height = viewport->Size.y;

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::to_imvec4(p.window));
    ImGui::Begin("##lock", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Clock and date, a little above the centre.
    float y = std::round(height * 0.26F);
    centered_text(format_time("%H:%M"), width, y, 96, ui::to_imu32(p.text));
    y += 112;
    centered_text(format_time("%A, %e %B"), width, y, 20, ui::to_imu32(p.text_dim));
    y += 80;
    centered_text(user_name_, width, y, 18, ui::to_imu32(p.text));
    y += 36;

    // Password field.
    const bool busy = auth_.busy();
    ImGui::SetCursorPos({std::round((width - kFieldWidth) / 2), y});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {12, 9});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ui::to_imvec4(p.surface));
    ImGui::BeginDisabled(busy);
    if (!busy && !ImGui::IsAnyItemActive()) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(kFieldWidth);
    const bool entered = ImGui::InputTextWithHint(
        "##password", "Password", password_.data(), password_.size(),
        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_EscapeClearsAll);
    if (ImGui::IsItemEdited() && !error_.empty()) {
        error_.clear();
    }
    ImGui::EndDisabled();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (entered) {
        // Drop the field's own copy of the text before wiping ours.
        ImGui::ClearActiveID();
        submit();
    }

    y += ImGui::GetFrameHeight() + 16;
    if (busy || auth_.busy()) {
        centered_text("Checking…", width, y, 15, ui::to_imu32(p.text_dim));
    } else if (!error_.empty()) {
        centered_text(error_, width, y, 15, ui::to_imu32(p.close_hover));
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

} // namespace tetra::shell
