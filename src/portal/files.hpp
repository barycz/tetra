#pragma once

// File system helpers for the file chooser, without UI: directory listings,
// places, the shared MIME database's globs, filters and URIs.

#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tetra::portal {

namespace fs = std::filesystem;

struct Entry {
    std::string name;
    bool directory = false; // also a symlink to one
    bool hidden = false;    // starts with '.' (or ends with '~', a backup)
    uint64_t size = 0;
    int64_t modified = 0; // seconds since the epoch
};

// The entries of `dir` (without "." and ".."); entries that vanish while
// being read are left out.
[[nodiscard]] std::expected<std::vector<Entry>, std::string> list_directory(const fs::path& dir);

struct Place {
    std::string label;
    fs::path path;
    std::string icon;
};
// Home, the XDG user dirs that exist (Desktop, Documents, ...), GTK
// bookmarks, mounted drives under /media/USER and /run/media/USER, and /.
// `home` and `config` are $HOME and $XDG_CONFIG_HOME (for the tests).
[[nodiscard]] std::vector<Place> places(const fs::path& home, const fs::path& config);
// XDG_*_DIR entries of user-dirs.dirs, e.g. {"DOCUMENTS", "/home/u/Documents"}.
[[nodiscard]] std::map<std::string, fs::path> user_dirs(const fs::path& home, const fs::path& config);

// The shared MIME database's file name globs (globs2) and subclasses: the
// type of a file by its name, and whether one type is a kind of another.
class MimeGlobs {
public:
    // Loads <dir>/globs2 and <dir>/subclasses (usually /usr/share/mime).
    void load(const fs::path& dir);
    [[nodiscard]] std::string type_of(std::string_view name) const; // "" if unknown
    // `type` is `wanted`, a subclass of it, or matches "major/*".
    [[nodiscard]] bool is_a(const std::string& type, const std::string& wanted) const;

private:
    struct Glob {
        int weight = 0;
        std::string pattern;
        std::string type;
        bool case_sensitive = false;
    };
    std::map<std::string, std::pair<int, std::string>> extensions_; // "png" -> weight, type
    std::vector<Glob> globs_;                                       // the rest
    std::multimap<std::string, std::string> parents_;
};

// A file chooser filter: (0, glob) and (1, MIME type) rules.
struct Filter {
    std::string name;
    std::vector<std::pair<uint32_t, std::string>> rules;
    bool operator==(const Filter&) const = default;
};
[[nodiscard]] bool matches(const Filter& filter, std::string_view name, const MimeGlobs& mime);

// file:// URI with everything but unreserved characters and '/' escaped.
[[nodiscard]] std::string file_uri(const fs::path& path);
// The path in a NUL-terminated byte array (portal "ay" options).
[[nodiscard]] std::optional<fs::path> path_from_bytes(const std::vector<uint8_t>& bytes);

// A symbolic icon name for an entry of the given MIME type.
[[nodiscard]] std::string icon_for(const Entry& entry, const std::string& type);

// "4.2 MB"; "12:31" today, "Nov 3" this year, else "2024-05-02".
[[nodiscard]] std::string format_size(uint64_t bytes);
[[nodiscard]] std::string format_time(int64_t seconds, int64_t now);

} // namespace tetra::portal
