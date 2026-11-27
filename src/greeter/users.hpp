#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <sys/types.h>

// Who can log in, and what the greeter remembers between logins.
namespace tetra::greeter {

struct User {
    std::string name;         // login name
    std::string display_name; // GECOS full name, else the login name
    uid_t uid = 0;
};

struct UidRange {
    uid_t min = 1000;
    uid_t max = 60000;
};

// UID_MIN / UID_MAX from login.defs (defaults when missing).
[[nodiscard]] UidRange read_uid_range(const std::filesystem::path& login_defs);
// People: accounts in `range` whose shell is not nologin/false, sorted by
// display name.
[[nodiscard]] std::vector<User> read_users(const std::filesystem::path& passwd, UidRange range);

// The last user and session (by session file ID).
struct State {
    std::string user;
    std::string session;
    bool operator==(const State&) const = default;
};
// $XDG_STATE_HOME/tetra/greeter.json, else ~/.local/state/...
[[nodiscard]] std::filesystem::path state_file();
[[nodiscard]] State load_state(const std::filesystem::path& path);
bool save_state(const std::filesystem::path& path, const State& state);

} // namespace tetra::greeter
