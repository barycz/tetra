#pragma once

#include <optional>
#include <string>
#include <vector>

// greetd's IPC (greetd-ipc(7)): every message, both ways, is a 32-bit
// length in native byte order followed by that many bytes of JSON.
namespace tetra::greeter {

[[nodiscard]] std::string frame(const std::string& json);
// Removes the complete messages from the front of `buffer` and returns
// their JSON; a partial message stays for the next read.
std::vector<std::string> unframe(std::string& buffer);

struct Reply {
    enum class Type { Invalid, Success, Error, AuthMessage };
    enum class Prompt { Visible, Secret, Info, Error };
    Type type = Type::Invalid;
    std::string error_type; // "auth_error" (wrong answer) or "error"
    std::string description;
    Prompt prompt = Prompt::Info;
    std::string message; // the PAM prompt or message
};
[[nodiscard]] Reply parse_reply(const std::string& json);

// Requests, as JSON.
[[nodiscard]] std::string create_session(const std::string& username);
// No response: acknowledges an info or error message.
[[nodiscard]] std::string post_auth_message_response(const std::optional<std::string>& response);
[[nodiscard]] std::string start_session(const std::vector<std::string>& cmd, const std::vector<std::string>& env);
[[nodiscard]] std::string cancel_session();

} // namespace tetra::greeter
