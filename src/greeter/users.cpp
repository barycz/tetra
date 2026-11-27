#include "users.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>

namespace tetra::greeter {

namespace fs = std::filesystem;

namespace {

std::vector<std::string_view> fields(std::string_view line, char sep) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (true) {
        const auto end = line.find(sep, start);
        out.push_back(line.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos) {
            return out;
        }
        start = end + 1;
    }
}

template <typename T>
bool parse_number(std::string_view text, T& value) {
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    return ec == std::errc{} && ptr == text.data() + text.size();
}

std::string lower(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

} // namespace

UidRange read_uid_range(const fs::path& login_defs) {
    UidRange range;
    std::ifstream file(login_defs);
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream words(line);
        std::string key;
        std::string value;
        if (!(words >> key >> value) || key.starts_with('#')) {
            continue;
        }
        uid_t number = 0;
        if (key == "UID_MIN" && parse_number(value, number)) {
            range.min = number;
        } else if (key == "UID_MAX" && parse_number(value, number)) {
            range.max = number;
        }
    }
    return range;
}

std::vector<User> read_users(const fs::path& passwd, UidRange range) {
    std::vector<User> users;
    std::ifstream file(passwd);
    std::string line;
    while (std::getline(file, line)) {
        // name:password:uid:gid:gecos:home:shell
        const auto f = fields(line, ':');
        if (f.size() < 7 || f[0].empty() || f[0].starts_with('#')) {
            continue;
        }
        uid_t uid = 0;
        if (!parse_number(f[2], uid) || uid < range.min || uid > range.max) {
            continue;
        }
        const std::string_view shell = f[6];
        if (shell.ends_with("/nologin") || shell.ends_with("/false") || shell.empty()) {
            continue;
        }
        const std::string_view full = f[4].substr(0, f[4].find(','));
        users.push_back({.name = std::string(f[0]),
                         .display_name = full.empty() ? std::string(f[0]) : std::string(full),
                         .uid = uid});
    }
    std::ranges::sort(users, [](const User& a, const User& b) {
        return std::pair(lower(a.display_name), a.name) < std::pair(lower(b.display_name), b.name);
    });
    return users;
}

fs::path state_file() {
    const char* state = std::getenv("XDG_STATE_HOME"); // NOLINT(concurrency-mt-unsafe)
    if (state != nullptr && *state == '/') {
        return fs::path(state) / "tetra/greeter.json";
    }
    const char* home = std::getenv("HOME"); // NOLINT(concurrency-mt-unsafe)
    return fs::path(home != nullptr ? home : "/") / ".local/state/tetra/greeter.json";
}

State load_state(const fs::path& path) {
    std::ifstream file(path);
    const auto data = nlohmann::json::parse(file, nullptr, false);
    State state;
    if (data.is_object()) {
        state.user = data.value("user", "");
        state.session = data.value("session", "");
    }
    return state;
}

bool save_state(const fs::path& path, const State& state) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const fs::path temporary = path.string() + ".tmp";
    {
        std::ofstream file(temporary, std::ios::trunc);
        file << nlohmann::json{{"user", state.user}, {"session", state.session}}.dump(2) << '\n';
        if (!file) {
            return false;
        }
    }
    fs::rename(temporary, path, ec);
    return !ec;
}

} // namespace tetra::greeter
