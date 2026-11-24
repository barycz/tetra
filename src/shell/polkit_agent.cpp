#include "polkit_agent.hpp"

#include "shell.hpp"
#include "style.hpp"
#include "window.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <print>
#include <span>
#include <string_view>

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ; // NOLINT

namespace tetra::shell {

namespace {

constexpr const char* kAgentPath = "/org/tetra/PolicyKit1/AuthenticationAgent";
constexpr const char* kAgentInterface = "org.freedesktop.PolicyKit1.AuthenticationAgent";
constexpr const char* kAuthorityInterface = "org.freedesktop.PolicyKit1.Authority";
constexpr const char* kCancelled = "org.freedesktop.PolicyKit1.Error.Cancelled";
constexpr const char* kFailed = "org.freedesktop.PolicyKit1.Error.Failed";
constexpr float kCardWidth = 440;
constexpr float kIconSize = 32;

// The setuid helper from polkit. TETRA_POLKIT_HELPER replaces it in tests:
// that grants nothing, because polkitd only accepts the outcome from the
// real helper (it reports to polkitd itself, as root).
std::string helper_path() {
    if (const char* env = std::getenv("TETRA_POLKIT_HELPER"); env != nullptr && *env != '\0') { // NOLINT
        return env;
    }
    for (const char* path : {"/usr/lib/polkit-1/polkit-agent-helper-1",
                             "/usr/libexec/polkit-agent-helper-1",
                             "/usr/lib/policykit-1/polkit-agent-helper-1"}) {
        if (access(path, X_OK) == 0) {
            return path;
        }
    }
    return {};
}

// The helper escapes its messages like g_strescape(); undo the common cases.
std::string unescape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 == text.size()) {
            out.push_back(text[i]);
            continue;
        }
        const char next = text[++i];
        out.push_back(next == 'n' ? '\n' : next == 't' ? '\t' : next);
    }
    return out;
}

std::string trim(std::string text) {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\n')) {
        text.pop_back();
    }
    return text;
}

std::string user_name(uid_t uid) {
    passwd entry{};
    passwd* result = nullptr;
    std::array<char, 4096> buffer{};
    if (getpwuid_r(uid, &entry, buffer.data(), buffer.size(), &result) == 0 && result != nullptr) {
        return entry.pw_name;
    }
    return std::to_string(uid);
}

std::optional<uid_t> user_id(const char* name) {
    passwd entry{};
    passwd* result = nullptr;
    std::array<char, 4096> buffer{};
    if (getpwnam_r(name, &entry, buffer.data(), buffer.size(), &result) == 0 && result != nullptr) {
        return entry.pw_uid;
    }
    return std::nullopt;
}

} // namespace

PolkitAgent::PolkitAgent(Shell& shell, sdbus::IConnection& system_bus,
                         const std::string& session_id)
    : shell_(shell), bus_(system_bus) {
    object_ = sdbus::createObject(bus_, sdbus::ObjectPath{kAgentPath});
    object_
        ->addVTable(
            sdbus::registerMethod("BeginAuthentication")
                .withInputParamNames("action_id", "message", "icon_name", "details", "cookie",
                                     "identities")
                .implementedAs([this](sdbus::Result<>&& result, std::string action_id,
                                      std::string message, std::string icon_name,
                                      const std::map<std::string, std::string>& /*details*/,
                                      std::string cookie, const IdentityList& identities) {
                    begin(std::move(result), std::move(action_id), std::move(message),
                          std::move(icon_name), std::move(cookie), identities);
                }),
            sdbus::registerMethod("CancelAuthentication")
                .withInputParamNames("cookie")
                .implementedAs([this](const std::string& cookie) { cancel(cookie); }))
        .forInterface(sdbus::InterfaceName{kAgentInterface});

    if (session_id.empty()) {
        std::println(stderr, "tetra-shell: no logind session; polkit agent not registered");
        return;
    }
    const char* locale = std::setlocale(LC_MESSAGES, nullptr);
    subject_ = {std::string("unix-session"),
                std::map<std::string, sdbus::Variant>{{"session-id", sdbus::Variant(session_id)}}};
    try {
        authority_ = sdbus::createProxy(bus_, sdbus::ServiceName{"org.freedesktop.PolicyKit1"},
                                        sdbus::ObjectPath{"/org/freedesktop/PolicyKit1/Authority"});
        authority_->callMethod("RegisterAuthenticationAgent")
            .onInterface(kAuthorityInterface)
            .withArguments(subject_, std::string(locale != nullptr ? locale : "C"),
                           std::string(kAgentPath));
        registered_ = true;
    } catch (const sdbus::Error& e) {
        // Another agent already serves this session, or no polkitd.
        std::println(stderr, "tetra-shell: polkit agent not registered: {}", e.what());
    }
}

PolkitAgent::~PolkitAgent() {
    stop_helper();
    window_.reset();
    for (Request& request : queue_) {
        request.result.returnError(sdbus::Error(sdbus::Error::Name{kCancelled}, "Agent exiting"));
    }
    queue_.clear();
    if (registered_) {
        try {
            authority_->callMethod("UnregisterAuthenticationAgent")
                .onInterface(kAuthorityInterface)
                .withArguments(subject_, std::string(kAgentPath));
        } catch (const sdbus::Error&) {
            // polkitd drops agents whose connection closes anyway
        }
    }
    explicit_bzero(response_.data(), response_.size());
}

std::vector<PolkitAgent::Identity> PolkitAgent::resolve(const IdentityList& identities) {
    std::vector<Identity> users;
    auto add = [&](uid_t uid) {
        if (std::ranges::none_of(users, [uid](const Identity& u) { return u.uid == uid; })) {
            users.push_back({uid, user_name(uid)});
        }
    };
    for (const auto& identity : identities) {
        const auto& kind = identity.get<0>();
        const auto& details = identity.get<1>();
        try {
            if (kind == "unix-user") {
                if (auto it = details.find("uid"); it != details.end()) {
                    add(it->second.get<uint32_t>());
                }
            } else if (kind == "unix-group") {
                // Admin rules usually name a group (sudo): offer its members.
                if (auto it = details.find("gid"); it != details.end()) {
                    group entry{};
                    group* result = nullptr;
                    std::vector<char> buffer(16384);
                    if (getgrgid_r(it->second.get<uint32_t>(), &entry, buffer.data(), buffer.size(),
                                   &result) == 0 &&
                        result != nullptr) {
                        for (char** member = entry.gr_mem; *member != nullptr; ++member) { // NOLINT
                            if (auto uid = user_id(*member)) {
                                add(*uid);
                            }
                        }
                    }
                }
            }
        } catch (const sdbus::Error&) {
            // a malformed identity: skip it
        }
    }
    // The current user first, if allowed: it is almost always the right one.
    const uid_t self = getuid();
    if (auto it = std::ranges::find_if(users, [self](const Identity& u) { return u.uid == self; });
        it != users.end()) {
        std::rotate(users.begin(), it, it + 1);
    }
    return users;
}

void PolkitAgent::begin(sdbus::Result<>&& result, std::string action_id, std::string message,
                        std::string icon_name, std::string cookie,
                        const IdentityList& identities) {
    auto users = resolve(identities);
    if (users.empty()) {
        result.returnError(sdbus::Error(sdbus::Error::Name{kFailed}, "No user to authenticate as"));
        return;
    }
    queue_.push_back(Request{.action_id = std::move(action_id),
                             .message = std::move(message),
                             .icon_name = std::move(icon_name),
                             .cookie = std::move(cookie),
                             .identities = std::move(users),
                             .selected = 0,
                             .result = std::move(result)});
    if (queue_.size() == 1) {
        show_next();
    }
}

void PolkitAgent::cancel(const std::string& cookie) {
    if (!queue_.empty() && queue_.front().cookie == cookie) {
        finish(false, kCancelled);
        return;
    }
    auto it = std::ranges::find_if(queue_, [&](const Request& r) { return r.cookie == cookie; });
    if (it != queue_.end()) {
        it->result.returnError(sdbus::Error(sdbus::Error::Name{kCancelled}, "Cancelled"));
        queue_.erase(it);
    }
}

void PolkitAgent::show_next() {
    if (queue_.empty()) {
        return;
    }
    prompt_.clear();
    info_.clear();
    error_.clear();
    window_ = std::make_unique<ui::LayerWindow>(
        shell_.app(), ui::LayerWindow::Options{
                          .layer = ui::LayerWindow::Layer::Overlay,
                          .anchor = ui::LayerWindow::kTop | ui::LayerWindow::kBottom |
                                    ui::LayerWindow::kLeft | ui::LayerWindow::kRight,
                          .width = 0,
                          .height = 0,
                          .exclusive_zone = -1,
                          .keyboard = ui::LayerWindow::Keyboard::Exclusive,
                          .name_space = "tetra-polkit",
                          .output = nullptr, // the output under the pointer
                      });
    window_->on_draw = [this] { draw(); };
    window_->on_closed = [this] { finish(false, kCancelled); };
    start_helper();
}

void PolkitAgent::close_window() {
    if (window_) {
        // Possibly called from the window's own draw: destroy it later.
        std::shared_ptr<ui::LayerWindow> closing(std::move(window_));
        shell_.app().add_timer(std::chrono::milliseconds(0), [closing] {});
    }
}

void PolkitAgent::finish(bool success, const char* error) {
    stop_helper();
    explicit_bzero(response_.data(), response_.size());
    waiting_for_input_ = false;
    close_window();
    if (queue_.empty()) {
        return;
    }
    Request request = std::move(queue_.front());
    queue_.pop_front();
    if (success) {
        request.result.returnResults();
    } else {
        const std::string_view name = error != nullptr ? error : kFailed;
        request.result.returnError(sdbus::Error(
            sdbus::Error::Name{std::string(name)},
            name == kCancelled ? "Authentication dialog was dismissed by the user"
                               : "Authentication failed"));
    }
    show_next();
}

// --- the helper ------------------------------------------------------------------------

void PolkitAgent::start_helper() {
    stop_helper();
    waiting_for_input_ = false;
    prompt_.clear();
    const std::string path = helper_path();
    if (path.empty() || queue_.empty()) {
        error_ = "polkit-agent-helper-1 is not installed";
        return;
    }
    const Request& request = queue_.front();
    const std::string user = request.identities.at(request.selected).name;

    std::array<int, 2> sv{};
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv.data()) != 0) {
        error_ = "Cannot start the authentication helper";
        return;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, sv[1], STDIN_FILENO); // dup2 clears CLOEXEC
    posix_spawn_file_actions_adddup2(&actions, sv[1], STDOUT_FILENO);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t none;
    sigemptyset(&none);
    sigset_t all;
    sigfillset(&all);
    posix_spawnattr_setsigmask(&attr, &none);
    posix_spawnattr_setsigdefault(&attr, &all);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);

    std::string arg0 = path;
    std::string arg1 = user;
    std::array<char*, 3> argv = {arg0.data(), arg1.data(), nullptr};
    pid_t pid = -1;
    const int status = posix_spawn(&pid, path.c_str(), &actions, &attr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    close(sv[1]);
    if (status != 0) {
        close(sv[0]);
        error_ = "Cannot start the authentication helper";
        std::println(stderr, "tetra-shell: polkit: cannot run {}: {}", path, std::strerror(status));
        return;
    }
    fcntl(sv[0], F_SETFL, fcntl(sv[0], F_GETFL) | O_NONBLOCK);
    helper_ = Helper{.pid = pid, .fd = sv[0], .buffer = {}};
    shell_.app().watch_fd(sv[0], [this] { on_helper_readable(); });

    // The helper reads the cookie from stdin first (never on the command
    // line, where other users could see it).
    const std::string line = request.cookie + "\n";
    [[maybe_unused]] const auto n = send(sv[0], line.data(), line.size(), MSG_NOSIGNAL);
}

void PolkitAgent::stop_helper() {
    if (!helper_) {
        return;
    }
    shell_.app().unwatch_fd(helper_->fd);
    close(helper_->fd);
    // A no-op if it already exited; we share its real uid, so we may signal it.
    kill(helper_->pid, SIGKILL);
    waitpid(helper_->pid, nullptr, 0);
    helper_.reset();
}

void PolkitAgent::on_helper_readable() {
    if (!helper_) {
        return;
    }
    // handle_line() may replace the helper (retry) or end the request; only
    // an EOF from this one counts.
    const pid_t pid = helper_->pid;
    auto same_helper = [&] { return helper_ && helper_->pid == pid; };
    std::array<char, 1024> chunk{};
    bool eof = false;
    while (helper_) {
        const ssize_t n = recv(helper_->fd, chunk.data(), chunk.size(), 0);
        if (n > 0) {
            helper_->buffer.append(chunk.data(), static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
            eof = true;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    while (same_helper()) {
        const auto end = helper_->buffer.find('\n');
        if (end == std::string::npos) {
            break;
        }
        std::string line = helper_->buffer.substr(0, end);
        helper_->buffer.erase(0, end + 1);
        handle_line(line);
    }
    if (eof && same_helper()) {
        // Ended without SUCCESS/FAILURE (crashed, or killed by PAM limits).
        stop_helper();
        error_ = "Authentication failed";
        start_helper();
    }
    if (window_) {
        window_->request_redraw();
    }
}

void PolkitAgent::handle_line(const std::string& line) {
    auto rest = [&](std::string_view prefix) { return trim(unescape(line.substr(prefix.size()))); };
    if (line.starts_with("PAM_PROMPT_ECHO_OFF ")) {
        prompt_ = rest("PAM_PROMPT_ECHO_OFF ");
        echo_ = false;
        waiting_for_input_ = true;
    } else if (line.starts_with("PAM_PROMPT_ECHO_ON ")) {
        prompt_ = rest("PAM_PROMPT_ECHO_ON ");
        echo_ = true;
        waiting_for_input_ = true;
    } else if (line.starts_with("PAM_ERROR_MSG ")) {
        error_ = rest("PAM_ERROR_MSG ");
    } else if (line.starts_with("PAM_TEXT_INFO ")) {
        info_ = rest("PAM_TEXT_INFO ");
    } else if (line == "SUCCESS") {
        finish(true);
    } else if (line == "FAILURE") {
        // Wrong password: start over, with a fresh PAM conversation.
        const std::string message = error_.empty() || error_ == "Authentication failure"
                                        ? "Wrong password. Try again."
                                        : error_;
        start_helper();
        error_ = message;
    }
}

void PolkitAgent::respond() {
    if (!helper_ || !waiting_for_input_) {
        return;
    }
    std::string line(response_.data());
    line.push_back('\n');
    [[maybe_unused]] const auto n = send(helper_->fd, line.data(), line.size(), MSG_NOSIGNAL);
    explicit_bzero(line.data(), line.size());
    explicit_bzero(response_.data(), response_.size());
    waiting_for_input_ = false;
    error_.clear();
    info_.clear();
}

// --- the dialog ------------------------------------------------------------------------

void PolkitAgent::draw() {
    if (queue_.empty()) {
        return;
    }
    Request& request = queue_.front();
    const auto& p = shell_.app().palette();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Dim everything behind the dialog.
    ImGui::GetBackgroundDrawList()->AddRectFilled({0, 0}, viewport->Size,
                                                  ui::to_imu32(config::Color{0, 0, 0, 0.45F}));

    ImGui::SetNextWindowPos({viewport->Size.x / 2, viewport->Size.y / 2}, ImGuiCond_Always,
                            {0.5F, 0.5F});
    ImGui::SetNextWindowSize({kCardWidth, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {22, 20});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::to_imvec4(p.surface));
    ImGui::Begin("##polkit", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);

    // Header: icon and title.
    const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
    const ImVec2 header = ImGui::GetCursorScreenPos();
    const std::string& icon_name =
        request.icon_name.empty() ? std::string("dialog-password") : request.icon_name;
    const ui::Icon* icon = shell_.icons().get(icon_name, static_cast<int>(kIconSize * scale));
    if (icon == nullptr) {
        icon = shell_.icons().get("dialog-password-symbolic", static_cast<int>(kIconSize * scale));
    }
    if (icon != nullptr) {
        const ImU32 tint = icon->symbolic ? ui::to_imu32(p.text) : IM_COL32_WHITE;
        ImGui::GetWindowDrawList()->AddImage(icon->ref(), header,
                                             {header.x + kIconSize, header.y + kIconSize}, {0, 0},
                                             {1, 1}, tint);
    }
    ImGui::SetCursorScreenPos({header.x + kIconSize + 14, header.y + 5});
    ImGui::PushFont(nullptr, 18);
    ImGui::TextUnformatted("Authentication Required");
    ImGui::PopFont();
    ImGui::SetCursorScreenPos({header.x, header.y + kIconSize + 14});

    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(request.message.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Dummy({0, 8});

    // Who: the current user, or a choice (e.g. members of the sudo group).
    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.text_dim));
    if (request.identities.size() == 1) {
        ImGui::Text("Authenticating as %s", request.identities.front().name.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::TextUnformatted("Authenticate as");
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth(-1);
        const std::string& current = request.identities.at(request.selected).name;
        if (ImGui::BeginCombo("##user", current.c_str())) {
            for (std::size_t i = 0; i < request.identities.size(); ++i) {
                if (ImGui::Selectable(request.identities[i].name.c_str(), i == request.selected) &&
                    i != request.selected) {
                    request.selected = i;
                    error_.clear();
                    start_helper(); // a new PAM conversation for that user
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::Dummy({0, 2});

    // The helper's prompt ("Password:") and the answer.
    ImGui::TextUnformatted(prompt_.empty() ? "Password:" : prompt_.c_str());
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {10, 8});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6);
    ImGui::BeginDisabled(!waiting_for_input_);
    if (waiting_for_input_ && !ImGui::IsAnyItemActive()) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-1);
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue;
    if (!echo_) {
        flags |= ImGuiInputTextFlags_Password;
    }
    const bool entered =
        ImGui::InputText("##response", response_.data(), response_.size(), flags);
    ImGui::EndDisabled();
    ImGui::PopStyleVar(2);

    if (!error_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.close_hover));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(error_.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    } else if (!info_.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(info_.c_str());
        ImGui::PopTextWrapPos();
    } else if (!waiting_for_input_ && helper_) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.text_dim));
        ImGui::TextUnformatted("Checking…");
        ImGui::PopStyleColor();
    }

    // What is being authorized, for the curious.
    ImGui::Dummy({0, 4});
    ImGui::PushFont(nullptr, 12);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.text_dim));
    ImGui::TextUnformatted(request.action_id.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy({0, 6});

    // Buttons, right-aligned: Cancel, Authenticate.
    const ImVec2 button{110, 0};
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x -
                         (2 * button.x) - spacing);
    const bool cancelled = ImGui::Button("Cancel", button);
    ImGui::SameLine();
    ImGui::BeginDisabled(!waiting_for_input_);
    ImGui::PushStyleColor(ImGuiCol_Button, ui::to_imvec4(p.accent));
    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.accent_text));
    const bool authenticate = ImGui::Button("Authenticate", button);
    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    if (cancelled || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        finish(false, kCancelled);
    } else if (entered || authenticate) {
        ImGui::ClearActiveID(); // drop the field's own copy of the text
        respond();
    }
}

} // namespace tetra::shell
