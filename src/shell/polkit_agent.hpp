#pragma once

#include <sdbus-c++/sdbus-c++.h>

#include <array>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <sys/types.h>
#include <vector>

namespace tetra::ui {
class LayerWindow;
}

namespace tetra::shell {

class Shell;

// polkit authentication agent: when an app asks polkitd for an action that
// needs a password (pkexec, mounting, package installs), polkitd calls
// BeginAuthentication on us. We run polkit-agent-helper-1 (setuid root),
// relay its PAM prompts to a dialog, and on success the helper itself tells
// polkitd. Requests are shown one at a time.
class PolkitAgent {
public:
    // Registers with polkitd for `session_id` (empty: export only).
    PolkitAgent(Shell& shell, sdbus::IConnection& system_bus, const std::string& session_id);
    ~PolkitAgent();

    PolkitAgent(const PolkitAgent&) = delete;
    PolkitAgent& operator=(const PolkitAgent&) = delete;
    PolkitAgent(PolkitAgent&&) = delete;
    PolkitAgent& operator=(PolkitAgent&&) = delete;

private:
    struct Identity {
        uid_t uid;
        std::string name;
    };
    struct Request {
        std::string action_id;
        std::string message;
        std::string icon_name;
        std::string cookie;
        std::vector<Identity> identities;
        std::size_t selected = 0;
        sdbus::Result<> result;
    };
    // One run of polkit-agent-helper-1: the PAM conversation for one try.
    struct Helper {
        pid_t pid = -1;
        int fd = -1; // socket: the helper's stdin and stdout
        std::string buffer;
    };

    using IdentityList = std::vector<sdbus::Struct<std::string, std::map<std::string, sdbus::Variant>>>;
    void begin(sdbus::Result<>&& result, std::string action_id, std::string message,
               std::string icon_name, std::string cookie, const IdentityList& identities);
    void cancel(const std::string& cookie);
    static std::vector<Identity> resolve(const IdentityList& identities);

    void show_next();
    void finish(bool success, const char* error = nullptr);
    void start_helper();
    void stop_helper();
    void on_helper_readable();
    void handle_line(const std::string& line);
    void respond();

    void draw();
    void close_window();

    Shell& shell_;
    sdbus::IConnection& bus_;
    std::unique_ptr<sdbus::IObject> object_;
    std::unique_ptr<sdbus::IProxy> authority_;
    sdbus::Struct<std::string, std::map<std::string, sdbus::Variant>> subject_;
    bool registered_ = false;

    std::deque<Request> queue_;           // front: the one on screen
    std::unique_ptr<ui::LayerWindow> window_;
    std::optional<Helper> helper_;
    std::string prompt_;                   // from the helper, e.g. "Password:"
    bool echo_ = false;                    // the prompt may be shown in clear
    bool waiting_for_input_ = false;       // a prompt is pending
    std::string info_;                     // PAM_TEXT_INFO
    std::string error_;                    // PAM_ERROR_MSG or our own
    std::array<char, 256> response_{};
};

} // namespace tetra::shell
