#include "files.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <format>
#include <fstream>
#include <ranges>

namespace tetra::portal {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::string percent_decode(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int hi = hex_value(s[i + 1]);
            const int lo = hex_value(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi * 16) + lo);
                i += 2;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

bool is_dir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

} // namespace

std::expected<std::vector<Entry>, std::string> list_directory(const fs::path& dir) {
    DIR* d = ::opendir(dir.c_str());
    if (d == nullptr) {
        return std::unexpected(std::string(std::strerror(errno)));
    }
    std::vector<Entry> out;
    const int dfd = ::dirfd(d);
    while (const dirent* e = ::readdir(d)) {
        const std::string_view name = e->d_name; // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        if (name == "." || name == "..") {
            continue;
        }
        struct stat st {};
        if (::fstatat(dfd, e->d_name, &st, 0) != 0 && // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
            ::fstatat(dfd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) { // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
            continue; // gone
        }
        Entry entry;
        entry.name = std::string(name);
        entry.directory = S_ISDIR(st.st_mode);
        entry.hidden = name.starts_with('.') || name.ends_with('~');
        entry.size = entry.directory ? 0 : static_cast<uint64_t>(st.st_size);
        entry.modified = st.st_mtime;
        out.push_back(std::move(entry));
    }
    ::closedir(d);
    return out;
}

std::map<std::string, fs::path> user_dirs(const fs::path& home, const fs::path& config) {
    std::map<std::string, fs::path> out;
    std::ifstream file(config / "user-dirs.dirs");
    std::string line;
    while (std::getline(file, line)) {
        // XDG_DOCUMENTS_DIR="$HOME/Documents"
        if (!line.starts_with("XDG_")) {
            continue;
        }
        const auto eq = line.find('=');
        const auto suffix = line.rfind("_DIR", eq);
        if (eq == std::string::npos || suffix == std::string::npos || suffix + 4 != eq) {
            continue;
        }
        std::string value = line.substr(eq + 1);
        if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
            continue;
        }
        value = value.substr(1, value.size() - 2);
        fs::path path;
        if (value.starts_with("$HOME")) {
            path = home.string() + value.substr(5);
        } else if (value.starts_with('/')) {
            path = value;
        } else {
            continue;
        }
        path = path.lexically_normal();
        if (!path.has_filename() && path.has_parent_path()) {
            path = path.parent_path(); // "$HOME/" is $HOME
        }
        out.emplace(line.substr(4, suffix - 4), path);
    }
    return out;
}

std::vector<Place> places(const fs::path& home, const fs::path& config) {
    std::vector<Place> out;
    out.push_back({"Home", home, "user-home-symbolic"});
    const auto dirs = user_dirs(home, config);
    static constexpr std::array<std::array<const char*, 4>, 6> kDirs = {{
        {"DESKTOP", "Desktop", "user-desktop-symbolic", "Desktop"},
        {"DOCUMENTS", "Documents", "folder-documents-symbolic", "Documents"},
        {"DOWNLOAD", "Downloads", "folder-download-symbolic", "Downloads"},
        {"MUSIC", "Music", "folder-music-symbolic", "Music"},
        {"PICTURES", "Pictures", "folder-pictures-symbolic", "Pictures"},
        {"VIDEOS", "Videos", "folder-videos-symbolic", "Videos"},
    }};
    for (const auto& [key, label, icon, fallback] : kDirs) {
        auto it = dirs.find(key);
        const fs::path path = it != dirs.end() ? it->second : home / fallback;
        if (path.lexically_normal() != home.lexically_normal() && is_dir(path)) {
            out.push_back({label, path, icon});
        }
    }
    // GTK bookmarks: "file:///path/to/dir Optional label".
    std::ifstream bookmarks(config / "gtk-3.0" / "bookmarks");
    std::string line;
    while (std::getline(bookmarks, line)) {
        if (!line.starts_with("file://")) {
            continue;
        }
        const auto space = line.find(' ');
        const fs::path path = percent_decode(std::string_view(line).substr(7, space == std::string::npos
                                                                                  ? std::string::npos
                                                                                  : space - 7));
        if (!is_dir(path) || std::ranges::any_of(out, [&](const Place& p) { return p.path == path; })) {
            continue;
        }
        std::string label = space == std::string::npos ? path.filename().string() : line.substr(space + 1);
        out.push_back({label.empty() ? path.string() : label, path, "folder-symbolic"});
    }
    // Mounted drives.
    const std::string user = home.filename().string();
    for (const fs::path& base : {fs::path("/media") / user, fs::path("/run/media") / user}) {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(base, ec)) {
            if (entry.is_directory(ec)) {
                out.push_back({entry.path().filename().string(), entry.path(), "drive-harddisk-symbolic"});
            }
        }
    }
    out.push_back({"Computer", "/", "drive-harddisk-symbolic"});
    return out;
}

void MimeGlobs::load(const fs::path& dir) {
    std::ifstream globs(dir / "globs2");
    std::string line;
    while (std::getline(globs, line)) {
        if (line.empty() || line.starts_with('#')) {
            continue;
        }
        // weight:type:pattern[:flags]
        const auto a = line.find(':');
        const auto b = a == std::string::npos ? a : line.find(':', a + 1);
        if (b == std::string::npos) {
            continue;
        }
        Glob g;
        g.weight = std::atoi(line.substr(0, a).c_str());
        g.type = line.substr(a + 1, b - a - 1);
        const auto c = line.find(':', b + 1);
        g.pattern = line.substr(b + 1, c == std::string::npos ? std::string::npos : c - b - 1);
        g.case_sensitive = c != std::string::npos && line.find("cs", c) != std::string::npos;
        const bool simple = g.pattern.starts_with("*.") &&
                            g.pattern.find_first_of("*?[", 2) == std::string::npos && !g.case_sensitive;
        if (simple) {
            const std::string ext = lower(g.pattern.substr(2));
            auto it = extensions_.find(ext);
            if (it == extensions_.end() || it->second.first < g.weight) {
                extensions_[ext] = {g.weight, g.type};
            }
        } else {
            globs_.push_back(std::move(g));
        }
    }
    std::ranges::stable_sort(globs_, std::greater<>(), &Glob::weight);
    std::ifstream subclasses(dir / "subclasses");
    while (std::getline(subclasses, line)) {
        const auto space = line.find(' ');
        if (space != std::string::npos) {
            parents_.emplace(line.substr(0, space), line.substr(space + 1));
        }
    }
}

std::string MimeGlobs::type_of(std::string_view name) const {
    const std::string lowered = lower(name);
    int best_weight = -1;
    std::string best;
    // Longest extension first: "a.tar.gz" is a compressed tar, not just gzip.
    for (std::size_t dot = lowered.find('.'); dot != std::string::npos; dot = lowered.find('.', dot + 1)) {
        auto it = extensions_.find(lowered.substr(dot + 1));
        if (it != extensions_.end()) {
            best_weight = it->second.first;
            best = it->second.second;
            break;
        }
    }
    const std::string full(name);
    for (const Glob& g : globs_) {
        if (g.weight <= best_weight) {
            break;
        }
        if (::fnmatch(g.pattern.c_str(), full.c_str(), g.case_sensitive ? 0 : FNM_CASEFOLD) == 0) {
            return g.type;
        }
    }
    return best;
}

bool MimeGlobs::is_a(const std::string& type, const std::string& wanted) const {
    if (type.empty()) {
        return false;
    }
    if (type == wanted || (wanted.ends_with("/*") && type.starts_with(wanted.substr(0, wanted.size() - 1)))) {
        return true;
    }
    if (wanted == "text/plain" && type.starts_with("text/")) {
        return true; // every text/* is text/plain (shared-mime-info spec)
    }
    std::vector<std::string> todo{type};
    for (int depth = 0; depth < 8 && !todo.empty(); ++depth) {
        std::vector<std::string> next;
        for (const auto& t : todo) {
            auto [begin, end] = parents_.equal_range(t);
            for (auto it = begin; it != end; ++it) {
                if (it->second == wanted) {
                    return true;
                }
                next.push_back(it->second);
            }
        }
        todo = std::move(next);
    }
    return false;
}

bool matches(const Filter& filter, std::string_view name, const MimeGlobs& mime) {
    if (filter.rules.empty()) {
        return true;
    }
    const std::string full(name);
    std::optional<std::string> type; // looked up once, if needed
    for (const auto& [kind, value] : filter.rules) {
        if (kind == 0) {
            if (::fnmatch(value.c_str(), full.c_str(), FNM_CASEFOLD) == 0) {
                return true;
            }
        } else if (kind == 1) {
            if (!type) {
                type = mime.type_of(name);
            }
            if (mime.is_a(*type, value)) {
                return true;
            }
        }
    }
    return false;
}

std::string file_uri(const fs::path& path) {
    static constexpr std::string_view kHex = "0123456789ABCDEF";
    std::string out = "file://";
    for (const unsigned char c : path.string()) {
        if (std::isalnum(c) != 0 || c == '/' || c == '-' || c == '.' || c == '_' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0x0F];
        }
    }
    return out;
}

std::optional<fs::path> path_from_bytes(const std::vector<uint8_t>& bytes) {
    std::string s(bytes.begin(), bytes.end());
    while (!s.empty() && s.back() == '\0') {
        s.pop_back();
    }
    if (s.empty() || s.find('\0') != std::string::npos) {
        return std::nullopt;
    }
    return fs::path(s);
}

std::string icon_for(const Entry& entry, const std::string& type) {
    if (entry.directory) {
        return "folder-symbolic";
    }
    if (type.starts_with("image/")) {
        return "image-x-generic-symbolic";
    }
    if (type.starts_with("audio/")) {
        return "audio-x-generic-symbolic";
    }
    if (type.starts_with("video/")) {
        return "video-x-generic-symbolic";
    }
    static constexpr std::array kArchives = {"zip", "x-tar", "gzip", "x-7z", "x-rar", "x-xz", "x-bzip", "zstd",
                                             "x-compressed-tar", "x-xz-compressed-tar", "vnd.debian.binary-package"};
    for (const char* a : kArchives) {
        if (type == std::string("application/") + a) {
            return "package-x-generic-symbolic";
        }
    }
    if (type == "application/pdf" || type.starts_with("application/vnd.oasis.opendocument") ||
        type.starts_with("application/vnd.openxmlformats") || type == "application/msword") {
        return "x-office-document-symbolic";
    }
    if (type == "application/x-executable" || type == "application/x-sharedlib") {
        return "application-x-executable-symbolic";
    }
    return "text-x-generic-symbolic";
}

std::string format_size(uint64_t bytes) {
    static constexpr std::array kUnits = {"bytes", "kB", "MB", "GB", "TB"};
    auto value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1000 && unit + 1 < kUnits.size()) {
        value /= 1000;
        ++unit;
    }
    if (unit == 0) {
        return bytes == 1 ? std::string("1 byte") : std::format("{} bytes", bytes);
    }
    return value < 99.95 ? std::format("{:.1f} {}", value, kUnits.at(unit))
                         : std::format("{:.0f} {}", value, kUnits.at(unit));
}

std::string format_time(int64_t seconds, int64_t now) {
    std::tm t{};
    std::tm n{};
    const auto ts = static_cast<std::time_t>(seconds);
    const auto ns = static_cast<std::time_t>(now);
    ::localtime_r(&ts, &t);
    ::localtime_r(&ns, &n);
    std::array<char, 64> buffer{};
    const char* format = (t.tm_year == n.tm_year && t.tm_yday == n.tm_yday) ? "%H:%M"
                         : t.tm_year == n.tm_year                            ? "%b %e"
                                                                             : "%Y-%m-%d";
    const std::size_t len = std::strftime(buffer.data(), buffer.size(), format, &t);
    std::string out(buffer.data(), len);
    // "%e" pads single days with a space: "Nov  3" -> "Nov 3".
    if (const auto pos = out.find("  "); pos != std::string::npos) {
        out.erase(pos, 1);
    }
    return out;
}

} // namespace tetra::portal
